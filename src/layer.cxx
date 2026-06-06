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

#include "layer.hxx"
#include "output.hxx"
#include "server.hxx"
#include "view.hxx"

using namespace steppewm;

// give keyboard focus to a layer surface like slurp
static void layer_surface_focus(layer_surface* ls) {
    server* s = ls->out->srv;
    struct wlr_seat* seat = s->seat;
    struct wlr_surface *surface = ls->wlr_layer_surface->surface;

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }
    s->focused_layer = ls;
}

// configure a steppewm_layer_surface
void layer_surface::configure() const {
    struct wlr_box box;
    wlr_output_layout_get_box(out->srv->output_layout, out->wlr_output, &box);
    if (box.width <= 0 || box.height <= 0) {
        return;
    }
    struct wlr_box full_area = {0, 0, box.width, box.height};
    struct wlr_box usable_area = full_area;
    wlr_scene_layer_surface_v1_configure(scene_layer_surface, &full_area, &usable_area);
}

// commit the layer surface
static void layer_commit(struct wl_listener *listener, void *data) {
    (void) data;
    layer_surface* ls = wl_container_of(listener, ls, commit);
    if (ls->wlr_layer_surface->initial_commit) {
        ls->configure();
    }
}

// map the layer surface
// claim keyboard focus if the client asked for it
static void layer_map(struct wl_listener *listener, void *data) {
    (void) data;
    layer_surface* ls = wl_container_of(listener, ls, map);
    if (ls->wlr_layer_surface->current.keyboard_interactive !=
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
        layer_surface_focus(ls);
    }
}

// unmap the layer surface
// hand keyboard focus back to a window
static void layer_unmap(struct wl_listener *listener, void *data) {
    (void) data;
    layer_surface* ls = wl_container_of(listener, ls, unmap);
    server* s = ls->out->srv;
    if (s->focused_layer == ls) {
        s->focused_layer = nullptr;
        view::focus_next(s, nullptr);
    }
}

// destroy layer
static void layer_destroy(struct wl_listener *listener, void *data) {
    (void) data;
    layer_surface* ls = wl_container_of(listener, ls, destroy);
    // guard against a dangling pointer
    if (ls->out->srv->focused_layer == ls) {
        ls->out->srv->focused_layer = nullptr;
    }
    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->unmap.link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    wl_list_remove(&ls->link);
    delete ls;
}

// create new steppewm_layer_surface
void layer_surface::on_new(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, new_layer_surface);
    struct wlr_layer_surface_v1* wlr_ls = static_cast<struct wlr_layer_surface_v1*>(data);

    // assign an output if the client didn't request a specific one
    if (!wlr_ls->output) {
        output* o;
        wl_list_for_each(o, &s->outputs, link) {
            wlr_ls->output = o->wlr_output;
            break;
        }
    }

    // if no outputs available
    if (!wlr_ls->output) {
        wlr_log(WLR_ERROR, "layer surface: no output available");
        wlr_layer_surface_v1_destroy(wlr_ls);
        return;
    }

    // check if the output for this layer surface is the same as one of the wlr_outputs
    output* out = nullptr;
    output* o;
    wl_list_for_each(o, &s->outputs, link) {
        if (o->wlr_output == wlr_ls->output) {
            out = o;
            break;
        }
    }

    // if none were
    if (!out) {
        wlr_log(WLR_ERROR, "layer surface: output not found");
        wlr_layer_surface_v1_destroy(wlr_ls);
        return;
    }

    uint32_t layer = wlr_ls->pending.layer;

    // create scene tree for this layer
    // each output has one scene tree per layer
    if (!out->layer_trees[layer]) {
        out->layer_trees[layer] = wlr_scene_tree_create(&s->scene->tree);

        // background renders at the bottom
        if (layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) {
            wlr_scene_node_lower_to_bottom(&out->layer_trees[layer]->node);
        } else if (layer == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM) {
            // above bg but under windows
            if (out->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]) {
                wlr_scene_node_place_above(
                    &out->layer_trees[layer]->node,
                    &out->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]->node);
            } else {
                wlr_scene_node_lower_to_bottom(&out->layer_trees[layer]->node);
            }
        } else {
            // TOP and OVERLAY are above windows and taskbar
            wlr_scene_node_raise_to_top(&out->layer_trees[layer]->node);
        }

        // align the layer tree with the output's position in the global scene
        struct wlr_box box;
        wlr_output_layout_get_box(s->output_layout, out->wlr_output, &box);
        if (box.width > 0) {
            wlr_scene_node_set_position(&out->layer_trees[layer]->node, box.x, box.y);
        }
    }

    // allocate memory for this layer surface
    auto* ls = new layer_surface();
    ls->out = out;
    ls->wlr_layer_surface = wlr_ls;
    wlr_ls->data = ls;

    // create the layer surface scene graph
    ls->scene_layer_surface = wlr_scene_layer_surface_v1_create(out->layer_trees[layer], wlr_ls);

    // add it to its output
    wl_list_insert(&out->layer_surfaces, &ls->link);

    // listen for commits
    ls->commit.notify = layer_commit;
    wl_signal_add(&wlr_ls->surface->events.commit, &ls->commit);

    // listen for map/unmap to manage keyboard focus
    ls->map.notify = layer_map;
    wl_signal_add(&wlr_ls->surface->events.map, &ls->map);
    ls->unmap.notify = layer_unmap;
    wl_signal_add(&wlr_ls->surface->events.unmap, &ls->unmap);

    // listen for layer destruction
    ls->destroy.notify = layer_destroy;
    wl_signal_add(&wlr_ls->events.destroy, &ls->destroy);
}
