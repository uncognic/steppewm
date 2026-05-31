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

#pragma once

#include <wayland-server-core.h>

struct steppewm_output;
struct wlr_layer_surface_v1;
struct wlr_scene_layer_surface_v1;

struct steppewm_layer_surface {
    struct steppewm_output *output;
    struct wlr_layer_surface_v1 *wlr_layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer_surface;

    struct wl_list link;

    struct wl_listener commit;
    struct wl_listener destroy;
};

void layer_surface_new(struct wl_listener *listener, void *data);
void layer_surface_configure(struct steppewm_layer_surface *ls);
