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

#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "layer.h"
#include "output.h"
#include "server.h"

// configure a steppewm_layer_surface
void layer_surface_configure(struct steppewm_layer_surface *ls) {
    struct steppewm_output *output = ls->output;
    struct wlr_box box;
    wlr_output_layout_get_box(output->server->output_layout, output->wlr_output, &box);
    if (box.width <= 0 || box.height <= 0) {
        return;
    }
    struct wlr_box full_area = {0, 0, box.width, box.height};
    struct wlr_box usable_area = full_area;
    wlr_scene_layer_surface_v1_configure(ls->scene_layer_surface, &full_area, &usable_area);
}

// commit the layer surface
static void layer_commit(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_layer_surface *ls = wl_container_of(listener, ls, commit);
    if (ls->wlr_layer_surface->initial_commit) {
        layer_surface_configure(ls);
    }
}

// destroy layer
static void layer_destroy(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_layer_surface *ls = wl_container_of(listener, ls, destroy);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    wl_list_remove(&ls->link);
    free(ls);
}

// create new steppewm_layer_surface
void layer_surface_new(struct wl_listener *listener, void *data) {
    struct steppewm_server *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *wlr_ls = data;

    // assign an output if the client didn't request a specific one
    if (!wlr_ls->output) {
        struct steppewm_output *o;
        wl_list_for_each(o, &server->outputs, link) {
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
    struct steppewm_output *output = nullptr;
    struct steppewm_output *o;
    wl_list_for_each(o, &server->outputs, link) {
        if (o->wlr_output == wlr_ls->output) {
            output = o;
            break;
        }
    }

    // if none were
    if (!output) {
        wlr_log(WLR_ERROR, "layer surface: output not found");
        wlr_layer_surface_v1_destroy(wlr_ls);
        return;
    }

    uint32_t layer = wlr_ls->pending.layer;

    // create scene tree for this layer
    // each output has one scene tree per layer
    if (!output->layer_trees[layer]) {
        output->layer_trees[layer] = wlr_scene_tree_create(&server->scene->tree);

        // background renders at the bottom
        if (layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) {
            wlr_scene_node_lower_to_bottom(&output->layer_trees[layer]->node);
        } else if (layer == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM) {
            // above bg but under windows
            if (output->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]) {
                wlr_scene_node_place_above(
                    &output->layer_trees[layer]->node,
                    &output->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]->node);
            } else {
                wlr_scene_node_lower_to_bottom(&output->layer_trees[layer]->node);
            }
        } else {
            // TOP and OVERLAY are above windows and taskbar
            wlr_scene_node_raise_to_top(&output->layer_trees[layer]->node);
        }

        // align the layer tree with the output's position in the global scene
        struct wlr_box box;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
        if (box.width > 0) {
            wlr_scene_node_set_position(&output->layer_trees[layer]->node, box.x, box.y);
        }
    }

    // allocate memory for this layer surface
    struct steppewm_layer_surface *ls = calloc(1, sizeof(*ls));
    ls->output = output;
    ls->wlr_layer_surface = wlr_ls;
    wlr_ls->data = ls;

    // create the layer surface scene graph
    ls->scene_layer_surface = wlr_scene_layer_surface_v1_create(output->layer_trees[layer], wlr_ls);

    // add it to its output
    wl_list_insert(&output->layer_surfaces, &ls->link);

    // listen for commits
    ls->commit.notify = layer_commit;
    wl_signal_add(&wlr_ls->surface->events.commit, &ls->commit);

    // listen for layer destruction
    ls->destroy.notify = layer_destroy;
    wl_signal_add(&wlr_ls->events.destroy, &ls->destroy);
}
