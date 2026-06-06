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

#include "wlr.h" // must be first

#include <time.h>

#include "layer.h"
#include "output.h"
#include "server.h"
#include "taskbar.h"
#include "view.h"

using namespace steppewm;

// called on output frame events
static void output_frame(struct wl_listener *listener, void *data) {
    (void) data;
    output* out = wl_container_of(listener, out, frame);
    struct wlr_scene_output *scene_output =
        wlr_scene_get_scene_output(out->srv->scene, out->wlr_output);

    wlr_scene_output_commit(scene_output, nullptr);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
    output* out = wl_container_of(listener, out, request_state);
    const struct wlr_output_event_request_state* event =
        static_cast<const struct wlr_output_event_request_state*>(data);
    wlr_output_commit_state(out->wlr_output, event->state);
}

// clean up output
static void output_destroy(struct wl_listener *listener, void *data) {
    (void) data;
    output* out = wl_container_of(listener, out, destroy);

    // remove taskbar for listener (output)
    if (out->taskbar) {
        delete out->taskbar;
        out->taskbar = nullptr;
    }

    // destroy layer surfaces (sends closed to clients)
    layer_surface *ls, *tmp;
    wl_list_for_each_safe(ls, tmp, &out->layer_surfaces, link) {
        wlr_layer_surface_v1_destroy(ls->wlr_layer_surface);
    }

    // destroy layer trees
    for (int i = 0; i < 4; i++) {
        if (out->layer_trees[i]) {
            wlr_scene_node_destroy(&out->layer_trees[i]->node);
            out->layer_trees[i] = nullptr;
        }
    }

    wl_list_remove(&out->frame.link);
    wl_list_remove(&out->request_state.link);
    wl_list_remove(&out->destroy.link);
    wl_list_remove(&out->link);
    delete out;
}

// update geometry of taskbar and layer surfaces for each output
static void output_layout_change(struct wl_listener *listener, void *data) {
    (void) data;
    server* s = wl_container_of(listener, s, output_layout_change);
    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        struct wlr_box box;
        wlr_output_layout_get_box(s->output_layout, out->wlr_output, &box);
        if (box.width <= 0) {
            continue;
        }

        // reposition layer trees to the output's new global origin
        for (int i = 0; i < 4; i++) {
            if (out->layer_trees[i]) {
                wlr_scene_node_set_position(&out->layer_trees[i]->node, box.x, box.y);
            }
        }

        // reconfigure layer surfaces with new dimensions
        layer_surface* ls;
        wl_list_for_each(ls, &out->layer_surfaces, link) {
            ls->configure();
        }

        if (out->taskbar) {
            out->taskbar->update_geometry();
            out->taskbar->raise();
        }
    }
}

// register a layout change
void output::register_layout_change(server* s) {
    s->output_layout_change.notify = output_layout_change;
    wl_signal_add(&s->output_layout->events.change, &s->output_layout_change);
}

// add new output and set it up
void output::on_new(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, new_output);
    struct wlr_output* wlr_output = static_cast<struct wlr_output*>(data);

    wlr_output_init_render(wlr_output, s->allocator, s->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
    }
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    auto* out = new output();
    out->srv = s;
    out->wlr_output = wlr_output;
    wl_list_init(&out->layer_surfaces);

    out->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &out->frame);

    out->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &out->request_state);

    out->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &out->destroy);

    wl_list_insert(&s->outputs, &out->link);

    // create taskbar before add_auto
    // taskbar must exist before then to be psoitioned correctly
    bool is_primary = wl_list_length(&s->outputs) == 1;
    if (is_primary || s->cfg.taskbar_all_outputs) {
        out->taskbar = new steppewm::taskbar(s, wlr_output);
        view* v;
        wl_list_for_each(v, &s->views, link) {
            out->taskbar->view_added(v);
        }
    }

    struct wlr_output_layout_output *layout_output =
        wlr_output_layout_add_auto(s->output_layout, wlr_output);
    out->scene_output = wlr_scene_output_create(s->scene, wlr_output);
    wlr_scene_output_layout_add_output(s->scene_layout, layout_output, out->scene_output);

    wlr_log(WLR_INFO, "new output: %s", wlr_output->name);
}
