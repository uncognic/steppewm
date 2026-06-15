/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "wlr.hxx" // must be first

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

#include "config.hxx"
#include "input.hxx"
#include "layer.hxx"
#include "listener.hxx"
#include "lock.hxx"
#include "osd.hxx"
#include "output.hxx"
#include "server.hxx"
#include "taskbar.hxx"
#include "tray.hxx"
#include "view.hxx"
#include "volume.hxx"

using namespace steppewm;

// initialize server
bool server::init(server* s) {
    // create display
    s->display = wl_display_create();
    if (!s->display) {
        return false;
    }

    // create backend, renderer, allocator
    s->backend = wlr_backend_autocreate(wl_display_get_event_loop(s->display), &s->session);
    if (!s->backend) {
        wlr_log(WLR_ERROR, "failed to create backend");
        return false;
    }

    s->renderer = wlr_renderer_autocreate(s->backend);
    if (!s->renderer) {
        wlr_log(WLR_ERROR, "failed to create renderer");
        return false;
    }
    wlr_renderer_init_wl_display(s->renderer, s->display);

    s->allocator = wlr_allocator_autocreate(s->backend, s->renderer);
    if (!s->allocator) {
        wlr_log(WLR_ERROR, "failed to create allocator");
        return false;
    }

    // create wlr stuff
    wlr_compositor_create(s->display, 6, s->renderer);
    wlr_subcompositor_create(s->display);
    wlr_data_device_manager_create(s->display);
    wlr_primary_selection_v1_device_manager_create(s->display);

    // clipboard managers
    wlr_data_control_manager_v1_create(s->display);
    wlr_ext_data_control_manager_v1_create(s->display, 1);

    // wp_viewporter protocol
    wlr_viewporter_create(s->display);

    // wp_presentation protocol
    wlr_presentation_create(s->display, s->backend, 2);

    // wp_fractional_scale protocol
    wlr_fractional_scale_manager_v1_create(s->display, 1);

    // wp_single_pixel_buffer protocol
    wlr_single_pixel_buffer_manager_v1_create(s->display);

    // wp_linux_drm_syncobj protocol
    if (s->renderer->features.timeline && s->backend->features.timeline) {
        int drm_fd = wlr_renderer_get_drm_fd(s->renderer);
        if (drm_fd >= 0) {
            wlr_linux_drm_syncobj_manager_v1_create(s->display, 1, drm_fd);
        }
    }

    output::init(s);

    s->scene = wlr_scene_create();
    s->scene_layout = wlr_scene_attach_output_layout(s->scene, s->output_layout);

    s->drag_icon_tree = wlr_scene_tree_create(&s->scene->tree);

    float snap_color[] = {0.5f, 0.5f, 0.5f, 0.25f};
    s->snap_indicator = wlr_scene_rect_create(&s->scene->tree, 0, 0, snap_color);
    wlr_scene_node_set_enabled(&s->snap_indicator->node, false);

    view::init(s);
    layer_surface::init(s);

    s->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(s->cursor, s->output_layout);

    s->cursor_mgr = wlr_xcursor_manager_create(nullptr, 24);

    s->cursor_motion.notify = on_cursor_motion;
    s->cursor_motion_absolute.notify = on_cursor_motion_absolute;
    s->cursor_button.notify = on_cursor_button;
    s->cursor_axis.notify = on_cursor_axis;
    s->cursor_frame.notify = on_cursor_frame;
    wl_signal_add(&s->cursor->events.motion, &s->cursor_motion);
    wl_signal_add(&s->cursor->events.motion_absolute, &s->cursor_motion_absolute);
    wl_signal_add(&s->cursor->events.button, &s->cursor_button);
    wl_signal_add(&s->cursor->events.axis, &s->cursor_axis);
    wl_signal_add(&s->cursor->events.frame, &s->cursor_frame);

    wl_list_init(&s->keyboards);
    wl_list_init(&s->pointers);
    s->new_input.notify = on_new_input;
    wl_signal_add(&s->backend->events.new_input, &s->new_input);

    s->seat = wlr_seat_create(s->display, "seat0");
    s->request_set_cursor.notify = on_request_set_cursor;
    s->request_set_selection.notify = on_request_set_selection;
    s->request_set_primary_selection.notify = on_request_set_primary_selection;
    wl_signal_add(&s->seat->events.request_set_cursor, &s->request_set_cursor);
    wl_signal_add(&s->seat->events.request_set_selection, &s->request_set_selection);
    wl_signal_add(&s->seat->events.request_set_primary_selection,
                  &s->request_set_primary_selection);

    // drag and drop
    s->request_start_drag.notify = on_request_start_drag;
    s->start_drag.notify = on_start_drag;
    wl_signal_add(&s->seat->events.request_start_drag, &s->request_start_drag);
    wl_signal_add(&s->seat->events.start_drag, &s->start_drag);

    // wp_cursor_shape protocol
    struct wlr_cursor_shape_manager_v1* cursor_shape_mgr =
        wlr_cursor_shape_manager_v1_create(s->display, 1);
    s->request_set_shape.notify = on_request_set_shape;
    wl_signal_add(&cursor_shape_mgr->events.request_set_shape, &s->request_set_shape);

    pointer_constraint::init(s);
    idle_inhibitor::init(s);
    session_lock::init(s);

    return true;
}

int server::find_free_x_display() {
    for (int n = 0; n < 32; n++) {
        char sock[64], lock[64];
        snprintf(sock, sizeof(sock), "/tmp/.X11-unix/X%d", n);
        snprintf(lock, sizeof(lock), "/tmp/.X%d-lock", n);
        if (access(sock, F_OK) != 0 && access(lock, F_OK) != 0) {
            return n;
        }
    }
    return -1;
}

void server::start_xwayland() {
    const int n = find_free_x_display();
    if (n < 0) {
        wlr_log(WLR_ERROR, "xwayland-satellite: no free X display");
        return;
    }

    char display[16];
    snprintf(display, sizeof(display), ":%d", n);

    if (pid_t pid = fork(); pid == 0) {
        setsid();
        execlp("xwayland-satellite", "xwayland-satellite", display, static_cast<char*>(nullptr));
        _exit(1);
    }

    setenv("DISPLAY", display, true);
    wlr_log(WLR_INFO, "xwayland-satellite on %s", display);
}

static void setup_portals() {
    const char* config_home = getenv("XDG_CONFIG_HOME");
    char dir[512], path[512];
    if (config_home) {
        snprintf(dir, sizeof(dir), "%s/xdg-desktop-portal", config_home);
    } else {
        const char* home = getenv("HOME");
        if (!home) {
            return;
        }
        snprintf(dir, sizeof(dir), "%s/.config/xdg-desktop-portal", home);
    }
    snprintf(path, sizeof(path), "%s/steppewm-portals.conf", dir);

    if (access(path, F_OK) == 0) {
        return;
    }

    mkdir(dir, 0755);
    FILE* f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(f, "[preferred]\n"
               "default=gtk\n"
               "org.freedesktop.impl.portal.Screenshot=wlr\n"
               "org.freedesktop.impl.portal.ScreenCast=wlr\n");
    fclose(f);
    wlr_log(WLR_INFO, "wrote portal config: %s", path);
}

static void update_dbus_environment() {
    if (pid_t pid = fork(); pid == 0) {
        execlp("dbus-update-activation-environment", "dbus-update-activation-environment",
               "WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", "DISPLAY", static_cast<char*>(nullptr));
        _exit(1);
    }
}

void server::run(server* s) {
    // get a socket
    const char* socket = wl_display_add_socket_auto(s->display);
    if (!socket) {
        wlr_log(WLR_ERROR, "failed to create Wayland socket");
        return;
    }

    // start the backend
    if (!wlr_backend_start(s->backend)) {
        wlr_log(WLR_ERROR, "failed to start backend");
        return;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    setenv("XDG_SESSION_TYPE", "wayland", true);
    setenv("XDG_CURRENT_DESKTOP", "steppewm", true);
    wlr_log(WLR_INFO, "steppewm running on %s", socket);

    if (s->cfg.xwayland) {
        start_xwayland();
    }

    setup_portals();
    update_dbus_environment();

#ifdef HAVE_LIBPULSE
    s->vol_mon = volume_monitor::create(s);
#else
    s->vol_mon = nullptr;
#endif

#ifdef HAVE_SDBUS
    s->tray = tray_host::create(s);
#else
    s->tray = nullptr;
#endif

    taskbar::init_monitors(s);
    s->osd_overlay = new osd(s);

    // run exec()s after setting up environment
    s->cfg.run_execs();

    wl_display_run(s->display);
}

// clean up
void server::fini(server* s) {
    delete s->osd_overlay;
    taskbar::fini_monitors(s);
#ifdef HAVE_SDBUS
    delete static_cast<tray_host*>(s->tray);
    s->tray = nullptr;
#endif
#ifdef HAVE_LIBPULSE
    delete static_cast<volume_monitor*>(s->vol_mon);
    s->vol_mon = nullptr;
#endif
    wl_display_destroy_clients(s->display);
    wlr_scene_node_destroy(&s->scene->tree.node);
    wlr_xcursor_manager_destroy(s->cursor_mgr);
    wlr_cursor_destroy(s->cursor);
    wlr_output_layout_destroy(s->output_layout);
    wlr_allocator_destroy(s->allocator);
    wlr_renderer_destroy(s->renderer);
    wlr_backend_destroy(s->backend);
    wl_display_destroy(s->display);
}

// entry
int main(int argc, char* argv[]) {
    wlr_log_init(WLR_DEBUG, nullptr);

    const char* cli_config_path = nullptr;

    int opt;
    while ((opt = getopt(argc, argv, "c:")) != -1) {
        switch (opt) {
            case 'c':
                cli_config_path = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s [-c config_path]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    server svr = {};
    svr.cfg.set_defaults();

    char config_path[512];

    if (cli_config_path && cli_config_path[0]) {
        snprintf(config_path, sizeof(config_path), "%s", cli_config_path);
    } else {
        const char* config_home = getenv("XDG_CONFIG_HOME");

        if (config_home && config_home[0]) {
            snprintf(config_path, sizeof(config_path), "%s/steppewm/config.lua", config_home);
        } else {
            const char* home = getenv("HOME");
            snprintf(config_path, sizeof(config_path), "%s/.config/steppewm/config.lua",
                     home ? home : "/root");
        }
    }

    snprintf(svr.config_path, sizeof(svr.config_path), "%s", config_path);
    svr.cfg.load(svr.config_path);

    if (!server::init(&svr)) {
        return EXIT_FAILURE;
    }

    server::run(&svr);
    server::fini(&svr);

    return EXIT_SUCCESS;
}
