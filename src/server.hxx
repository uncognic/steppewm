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

#include "config.hxx"

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

struct wlr_input_device;

namespace steppewm {

class view;
class layer_surface;
class switcher;
class osd;

enum class cursor_mode {
    passthrough,
    move,
    resize,
};

class server {
  public:
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_session *session;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;

    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    // outputs
    struct wlr_output_layout *output_layout;
    struct wl_list outputs;
    struct wl_listener new_output;
    struct wl_listener output_layout_change;
    struct wlr_output_manager_v1* output_mgr;
    struct wl_listener output_mgr_apply;
    struct wl_listener output_mgr_test;
    struct wlr_output_power_manager_v1* output_power_mgr;
    struct wl_listener output_power_set_mode;
    struct wlr_gamma_control_manager_v1* gamma_control_mgr;
    struct wl_listener set_gamma;

    // xdg shell
    struct wlr_xdg_shell *xdg_shell;
    struct wlr_xdg_activation_v1* xdg_activation;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_listener request_activate;
    struct wl_list views;
    int cascade_n;
    int cascade_x;
    int current_workspace;

    // layer shell
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;
    layer_surface* focused_layer;

    // decorations
    struct wlr_xdg_decoration_manager_v1 *deco_manager;
    struct wl_listener new_deco;

    // xdg-toplevel-icon protocol
    struct wlr_xdg_toplevel_icon_manager_v1* icon_mgr;
    struct wl_listener set_icon;

    // non-null only when visible
    switcher* sw;

    // input
    struct wlr_seat *seat;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_list keyboards;
    struct wl_list pointers;
    uint32_t layout_group;

    struct wl_listener new_input;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener request_set_cursor;
    struct wl_listener request_set_shape;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;

    // drag and drop
    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wl_listener drag_destroy;
    struct wlr_scene_tree* drag_icon_tree;

    // pointer constraints
    struct wlr_relative_pointer_manager_v1* relative_pointer_mgr;
    struct wlr_pointer_constraints_v1* pointer_constraints;
    struct wlr_pointer_constraint_v1* active_constraint;
    struct wl_listener new_constraint;

    // idle
    struct wlr_idle_notifier_v1* idle_notifier;
    struct wlr_idle_inhibit_manager_v1* idle_inhibit_mgr;
    struct wl_listener new_idle_inhibitor;
    bool idle_inhibit_manual;

    struct wlr_session_lock_manager_v1* session_lock_mgr;
    struct wlr_session_lock_v1* cur_lock;
    struct wlr_scene_tree* lock_tree;
    struct wlr_scene_rect* lock_bg;
    struct wlr_scene_tree* lock_msg_tree;
    bool locked;
    struct wl_listener new_lock;

    // move/resize grab state
    cursor_mode grab_mode;
    view* grabbed_view;
    double grab_x, grab_y;
    struct wlr_box grab_geobox;
    uint32_t resize_edges;

    bool grab_restore_pending;
    double grab_start_x, grab_start_y;

    wlr_scene_rect* snap_indicator;

    void* vol_mon;
    void* tray;
    osd* osd_overlay;

    int brightness_watch_fd;
    wl_event_source* brightness_source;
    wl_event_source* indicator_timer;

    config cfg;
    char config_path[512];

    static bool init(server* s);
    static void run(server* s);
    static void fini(server* s);

    static void input_reconfigure(server* s);
    static void cursor_begin_interactive(view* v, cursor_mode mode, uint32_t edges);
    static bool handle_keybinding(server* s, uint32_t mods, xkb_keysym_t sym);

    static void spawn(const char* cmd);

  private:
    static void start_xwayland();
    static int find_free_x_display();
    static view* focused_view(server* s);
    static void dispatch_action(server* s, const char* action, const char* arg, uint32_t mods);
    static void on_new_input(struct wl_listener* listener, void* data);
    static void on_cursor_motion(struct wl_listener* listener, void* data);
    static void on_cursor_motion_absolute(struct wl_listener* listener, void* data);
    static void on_cursor_button(struct wl_listener* listener, void* data);
    static void on_cursor_axis(struct wl_listener* listener, void* data);
    static void on_cursor_frame(struct wl_listener* listener, void* data);
    static void process_cursor_move(server* s);
    static void process_cursor_resize(server* s);
    static void process_cursor_motion(server* s, uint32_t time_msec);
    static void cursor_move_relative(server* s, struct wlr_input_device* device, double dx,
                                     double dy, double unaccel_dx, double unaccel_dy,
                                     uint32_t time_msec);
    static void on_request_set_cursor(struct wl_listener* listener, void* data);
    static void on_request_set_shape(struct wl_listener* listener, void* data);
    static void on_request_set_selection(struct wl_listener* listener, void* data);
    static void on_request_set_primary_selection(struct wl_listener* listener, void* data);
    static void on_request_start_drag(struct wl_listener* listener, void* data);
    static void on_start_drag(struct wl_listener* listener, void* data);
    static void on_drag_destroy(struct wl_listener* listener, void* data);
};

} // namespace steppewm
