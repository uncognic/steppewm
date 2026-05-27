#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "deco.h"
#include "input.h"
#include "output.h"
#include "server.h"
#include "view.h"

// initialize server
static bool server_init(struct steppewm_server *s) {
    // create display
    s->display = wl_display_create();
    if (!s->display) {
        return false;
    }

    // create backend, renderer, allocator
    s->backend = wlr_backend_autocreate(wl_display_get_event_loop(s->display), NULL);
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

    // create output layout
    s->output_layout = wlr_output_layout_create(s->display);

    // create listeners and signals
    wl_list_init(&s->outputs);
    s->new_output.notify = output_new;
    wl_signal_add(&s->backend->events.new_output, &s->new_output);

    s->scene = wlr_scene_create();
    s->scene_layout = wlr_scene_attach_output_layout(s->scene, s->output_layout);

    wl_list_init(&s->views);
    s->xdg_shell = wlr_xdg_shell_create(s->display, 6);
    s->new_xdg_toplevel.notify = view_new;
    wl_signal_add(&s->xdg_shell->events.new_toplevel, &s->new_xdg_toplevel);

    s->deco_manager = wlr_xdg_decoration_manager_v1_create(s->display);
    s->new_deco.notify = deco_new;
    wl_signal_add(&s->deco_manager->events.new_toplevel_decoration, &s->new_deco);

    s->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(s->cursor, s->output_layout);

    s->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

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

    return true;
}

static void server_run(struct steppewm_server *s) {
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

    wl_display_run(s->display);
}

// clean up
static void server_fini(struct steppewm_server *s) {
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
int main(void) {
    wlr_log_init(WLR_DEBUG, NULL);

    struct steppewm_server server = {0};
    if (!server_init(&server)) {
        return EXIT_FAILURE;
    }

    server_run(&server);
    server_fini(&server);

    return EXIT_SUCCESS;
}