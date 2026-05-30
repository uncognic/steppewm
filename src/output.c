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

#include <stdlib.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "output.h"
#include "server.h"
#include "taskbar.h"
#include "view.h"

// called on output frame events
static void output_frame(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output =
        wlr_scene_get_scene_output(output->server->scene, output->wlr_output);

    wlr_scene_output_commit(scene_output, nullptr);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
    struct steppewm_output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

// clean up output
static void output_destroy(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_output *output = wl_container_of(listener, output, destroy);

    // remove taskbar for listener (output)
    if (output->taskbar) {
        taskbar_destroy(output->taskbar);
        output->taskbar = nullptr;
    }

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

// update geometry of taskbar for each output
static void output_layout_change(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_server *server = wl_container_of(listener, server, output_layout_change);
    struct steppewm_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->taskbar) {
            taskbar_update_geometry(output->taskbar);
            wlr_scene_node_raise_to_top(&output->taskbar->tree->node);
        }
    }
}

// register a layout change
void output_layout_change_register(struct steppewm_server *server) {
    server->output_layout_change.notify = output_layout_change;
    wl_signal_add(&server->output_layout->events.change, &server->output_layout_change);
}

// add new output and set it up
void output_new(struct wl_listener *listener, void *data) {
    struct steppewm_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
    }
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    struct steppewm_output *output = calloc(1, sizeof(*output));
    output->server = server;
    output->wlr_output = wlr_output;

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);

    // create taskbar before add_auto
    // taskbar must exist before then to be psoitioned correctly
    bool is_primary = wl_list_length(&server->outputs) == 1;
    if (is_primary || server->config.taskbar_all_outputs) {
        output->taskbar = taskbar_create(server, wlr_output);
        struct steppewm_view *view;
        wl_list_for_each(view, &server->views, link) {
            taskbar_view_added(output->taskbar, view);
        }
    }

    struct wlr_output_layout_output *layout_output =
        wlr_output_layout_add_auto(server->output_layout, wlr_output);
    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, layout_output, output->scene_output);

    wlr_log(WLR_INFO, "new output: %s", wlr_output->name);
}