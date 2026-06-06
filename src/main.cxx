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
#include <unistd.h>

#include "config.hxx"
#include "deco.hxx"
#include "input.hxx"
#include "layer.hxx"
#include "output.hxx"
#include "server.hxx"
#include "view.hxx"

using namespace steppewm;

// initialize server
static bool server_init(server* s) {
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

    // create output layout
    s->output_layout = wlr_output_layout_create(s->display);
    output::register_layout_change(s);

    // slurp needs xdg-output to enumerate output geometry
    // grim needs screencopy to capture pixels
    wlr_xdg_output_manager_v1_create(s->display, s->output_layout);
    wlr_screencopy_manager_v1_create(s->display);

    // create listeners and signals
    wl_list_init(&s->outputs);
    s->new_output.notify = output::on_new;
    wl_signal_add(&s->backend->events.new_output, &s->new_output);

    s->scene = wlr_scene_create();
    s->scene_layout = wlr_scene_attach_output_layout(s->scene, s->output_layout);

    wl_list_init(&s->views);
    s->xdg_shell = wlr_xdg_shell_create(s->display, 6);
    s->new_xdg_toplevel.notify = view::on_new;
    wl_signal_add(&s->xdg_shell->events.new_toplevel, &s->new_xdg_toplevel);
    s->new_xdg_popup.notify = popup::on_new;
    wl_signal_add(&s->xdg_shell->events.new_popup, &s->new_xdg_popup);

    s->layer_shell = wlr_layer_shell_v1_create(s->display, 4);
    s->new_layer_surface.notify = layer_surface::on_new;
    wl_signal_add(&s->layer_shell->events.new_surface, &s->new_layer_surface);

    s->deco_manager = wlr_xdg_decoration_manager_v1_create(s->display);
    s->new_deco.notify = deco_new;
    wl_signal_add(&s->deco_manager->events.new_toplevel_decoration, &s->new_deco);

    s->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(s->cursor, s->output_layout);

    s->cursor_mgr = wlr_xcursor_manager_create(nullptr, 24);

    s->cursor_motion.notify = cursor_motion;
    s->cursor_motion_absolute.notify = cursor_motion_absolute;
    s->cursor_button.notify = cursor_button;
    s->cursor_axis.notify = cursor_axis;
    s->cursor_frame.notify = cursor_frame;
    wl_signal_add(&s->cursor->events.motion, &s->cursor_motion);
    wl_signal_add(&s->cursor->events.motion_absolute, &s->cursor_motion_absolute);
    wl_signal_add(&s->cursor->events.button, &s->cursor_button);
    wl_signal_add(&s->cursor->events.axis, &s->cursor_axis);
    wl_signal_add(&s->cursor->events.frame, &s->cursor_frame);

    wl_list_init(&s->keyboards);
    wl_list_init(&s->pointers);
    s->new_input.notify = input_new;
    wl_signal_add(&s->backend->events.new_input, &s->new_input);

    s->seat = wlr_seat_create(s->display, "seat0");
    s->request_set_cursor.notify = request_set_cursor;
    s->request_set_selection.notify = request_set_selection;
    s->request_set_primary_selection.notify = request_set_primary_selection;
    wl_signal_add(&s->seat->events.request_set_cursor, &s->request_set_cursor);
    wl_signal_add(&s->seat->events.request_set_selection, &s->request_set_selection);
    wl_signal_add(&s->seat->events.request_set_primary_selection,
                  &s->request_set_primary_selection);

    // wp_cursor_shape protocol
    struct wlr_cursor_shape_manager_v1* cursor_shape_mgr =
        wlr_cursor_shape_manager_v1_create(s->display, 1);
    s->request_set_shape.notify = request_set_shape;
    wl_signal_add(&cursor_shape_mgr->events.request_set_shape, &s->request_set_shape);

    s->relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(s->display);

    s->pointer_constraints = wlr_pointer_constraints_v1_create(s->display);
    s->new_constraint.notify = new_pointer_constraint;
    wl_signal_add(&s->pointer_constraints->events.new_constraint, &s->new_constraint);

    return true;
}

static void server_run(server* s) {
    // get a socket
    const char *socket = wl_display_add_socket_auto(s->display);
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
    wlr_log(WLR_INFO, "steppewm running on %s", socket);

    // run exec()s after setting up environment
    s->cfg.run_execs();

    wl_display_run(s->display);
}

// clean up
static void server_fini(server* s) {
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
int main(int argc, char *argv[]) {
    wlr_log_init(WLR_DEBUG, nullptr);

    const char *cli_config_path = nullptr;

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
        const char *config_home = getenv("XDG_CONFIG_HOME");

        if (config_home && config_home[0]) {
            snprintf(config_path, sizeof(config_path), "%s/steppewm/config.lua", config_home);
        } else {
            const char *home = getenv("HOME");
            snprintf(config_path, sizeof(config_path), "%s/.config/steppewm/config.lua",
                     home ? home : "/root");
        }
    }

    snprintf(svr.config_path, sizeof(svr.config_path), "%s", config_path);
    svr.cfg.load(svr.config_path);

    if (!server_init(&svr)) {
        return EXIT_FAILURE;
    }

    server_run(&svr);
    server_fini(&svr);

    return EXIT_SUCCESS;
}
