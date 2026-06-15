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

struct wlr_output;
struct wlr_output_mode;
struct wlr_scene_output;
struct wlr_scene_tree;

namespace steppewm {

class server;
class output_config;
class taskbar;
class layer_surface;

class output {
  public:
    server* srv;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;

    taskbar* output_taskbar;

    struct wlr_scene_tree *layer_trees[4];
    struct wl_list layer_surfaces;

    struct wl_list link;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;

    void apply_config();
    void create_taskbar();

    static void init(server* s);
    static void on_new(struct wl_listener* listener, void* data);
    static void on_power_set_mode(struct wl_listener* listener, void* data);
    static void on_set_gamma(struct wl_listener* listener, void* data);
    static void register_layout_change(server* s);
    static void reconfigure_all(server* s);
    static void broadcast_output_config(server* s);
    static int taskbar_height(server* s, const struct wlr_output* wlr_out);

  private:
    static struct wlr_output_mode* pick_mode(struct wlr_output* wlr_output,
                                             const output_config* oc);
    static bool any_taskbar(server* s);
    static void on_frame(struct wl_listener* listener, void* data);
    static void on_request_state(struct wl_listener* listener, void* data);
    static void on_destroy(struct wl_listener* listener, void* data);
    static void on_layout_change(struct wl_listener* listener, void* data);
    static void on_output_mgr_apply(struct wl_listener* listener, void* data);
    static void on_output_mgr_test(struct wl_listener* listener, void* data);
    static void ensure_taskbars(const server* s);
    static output* find_for_wlr_output(const server* s, const struct wlr_output* wlr_out);
};

} // namespace steppewm
