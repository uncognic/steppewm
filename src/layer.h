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

struct wlr_layer_surface_v1;
struct wlr_scene_layer_surface_v1;

namespace steppewm {

class server;
class output;

class layer_surface {
  public:
    output* out;
    struct wlr_layer_surface_v1 *wlr_layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer_surface;

    struct wl_list link;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;

    static void init(server* s);
    static void on_new(struct wl_listener* listener, void* data);
    void configure() const;

  private:
    static void focus(layer_surface* ls);
    static void on_commit(struct wl_listener* listener, void* data);
    static void on_map(struct wl_listener* listener, void* data);
    static void on_unmap(struct wl_listener* listener, void* data);
    static void on_destroy(struct wl_listener* listener, void* data);
};

} // namespace steppewm
