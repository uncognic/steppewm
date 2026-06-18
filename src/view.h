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

#include "listener.h"

#include <wayland-server-core.h>
#include <wlr/util/box.h>

namespace steppewm {

class server;

enum class deco_mode {
    SERVER,
    CLIENT,
};

enum class snap_edge {
    NONE,
    LEFT,
    RIGHT,
    TOP_LEFT,
    TOP_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_RIGHT,
};

class deco {
  public:
    struct wlr_scene_rect *titlebar;
    struct wlr_scene_buffer *title_label;
    struct wlr_scene_rect *close_button;
    struct wlr_scene_rect *maximize;
    struct wlr_scene_rect *minimize;
    struct wlr_scene_rect *border_top;
    struct wlr_scene_rect *border_left;
    struct wlr_scene_rect *border_right;
    struct wlr_scene_rect *border_bottom;
    struct wlr_scene_rect *corner_tl;
    struct wlr_scene_rect *corner_tr;
    struct wlr_scene_rect *corner_bl;
    struct wlr_scene_rect *corner_br;
};

class popup {
  public:
    struct wlr_xdg_popup* xdg_popup;
    bool unconstrained;
    Listener commit;
    Listener reposition;
    Listener destroy;

    static void on_new(struct wl_listener* listener, void* data);

  private:
    static bool unconstrain(popup* p);
    static void handle_commit(popup* p);
    static void handle_reposition(popup* p);
    static void handle_destroy(popup* p);
};

// a window
class view {
  public:
    server* srv;
    struct wlr_xdg_toplevel *toplevel;
    struct wlr_scene_tree *scene_tree; // container: whole decorated window
    struct wlr_scene_tree *xdg_tree;   // content: the actual content w/o titlebar and borders
    deco_mode decoration_mode;

    deco window_decoration;
    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    struct wlr_xdg_toplevel_decoration_v1* pending_deco;
    Listener request_deco_mode;
    Listener destroy_deco;
    struct wl_event_source *initial_configure_idle;

    bool maximized;
    bool fullscreen;
    bool minimized;
    bool urgent;
    bool mapped;
    bool pinned;
    int workspace;
    snap_edge snapped;
    struct wlr_xdg_toplevel_icon_v1* icon;
    struct wlr_box saved_geo;

    struct wl_list link;

    Listener map;
    Listener unmap;
    Listener commit;
    Listener destroy;
    Listener request_move;
    Listener request_resize;
    Listener request_maximize;
    Listener request_fullscreen;
    Listener request_minimize;
    Listener title_changed;
    Listener app_id_changed;

    struct wlr_foreign_toplevel_handle_v1* foreign_handle;
    struct wlr_ext_foreign_toplevel_handle_v1* foreign_ext_handle;
    Listener ft_request_maximize;
    Listener ft_request_minimize;
    Listener ft_request_activate;
    Listener ft_request_fullscreen;
    Listener ft_request_close;

    // window operations
    void focus(struct wlr_surface* surface);
    void minimize(bool minimized);
    void set_urgent(bool urgent);
    void toggle_maximize();
    void toggle_fullscreen();
    void unmaximize_to_cursor(double cursor_x, double cursor_y);
    void update_visibility() const;
    void move_to_workspace(int workspace);
    void snap_to(snap_edge edge);

    void deco_create();
    void deco_update() const;
    void deco_destroy();
    void deco_set_focus(bool focused) const;
    void deco_set_hover(const struct wlr_scene_node* node, bool hovered) const;
    bool deco_is_button(const struct wlr_scene_node* node) const;
    void deco_set_visible(bool visible) const;
    const char* deco_cursor_name(const struct wlr_scene_node* node) const;
    bool deco_handle_button(server* s, const struct wlr_scene_node* node, uint32_t button,
                            uint32_t time_msec);

    static void init(server* s);
    static void on_new(struct wl_listener* listener, void* data);
    static void focus_next(server* s, const view* skip);
    static void reconfigure_all(server* s);
    static view* at(server* s, double lx, double ly, struct wlr_surface** surface, double* sx,
                    double* sy);
    static void handle_activation_request(struct wl_listener* listener, void* data);
    static void workspace_switch(server* s, int workspace);

    // event callback for new xdg decoration
    static void deco_new(struct wl_listener* listener, void* data);
    // returns the view and sets *node to the hit rect
    static view* deco_at(const server* s, double lx, double ly, struct wlr_scene_node** node);

  private:
    static bool can_configure(const view* v);
    static void raise_overlays(server* s);
    static void apply_state(view* v, bool maximized, bool fullscreen);
    static void apply_pending_deco(view* v);
    static void initial_configure(void* data);
    static void get_box(view* v, struct wlr_box* box);
    static void place(view* v);
    static void handle_map(view* v);
    static void handle_unmap(view* v);
    static void handle_commit(view* v);
    static void handle_destroy(view* v);
    static void handle_request_move(view* v);
    static void handle_request_resize(view* v, void* data);
    static void handle_request_maximize(view* v);
    static void handle_request_fullscreen(view* v);
    static void handle_request_minimize(view* v);
    static void handle_set_icon(struct wl_listener* listener, void* data);
    static view* from_surface(struct wlr_surface* surface);
    static bool activation_token_valid(server* s, struct wlr_xdg_activation_token_v1* token);
    static bool surface_is_view_focused(server* s, view* v);
    static void deco_render_title(struct wlr_scene_buffer* scene_buf, const char* text, int w,
                                  int h, float fg[4]);
    static void deco_request_mode(view* v, struct wlr_xdg_toplevel_decoration_v1* decoration);
    static void deco_handle_destroy(view* v);
};

} // namespace steppewm
