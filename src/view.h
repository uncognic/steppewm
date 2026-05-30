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

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct steppewm_server;
struct wlr_xdg_toplevel;
struct wlr_xdg_toplevel_decoration_v1;
struct wlr_scene_tree;
struct wlr_surface;
struct wlr_scene_rect;

struct steppewm_deco {
    struct wlr_scene_rect *titlebar;
    struct wlr_scene_rect *close_button;
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

enum steppewm_deco_mode {
    STEPPEWM_DECO_SERVER,
    STEPPEWM_DECO_CLIENT,
};

// a window
struct steppewm_view {
    struct steppewm_server *server;
    struct wlr_xdg_toplevel *toplevel;
    struct wlr_scene_tree *scene_tree; // container: whole decorated window
    struct wlr_scene_tree *xdg_tree;   // content: the actual content w/o titlebar and borders
    enum steppewm_deco_mode deco_mode;

    struct steppewm_deco deco;                           // the decoration
    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    struct wlr_xdg_toplevel_decoration_v1 *pending_deco; // applied once configure events are legal
    struct wl_listener request_deco_mode;                // for xdg-decoration request_mode
    struct wl_listener destroy_deco;
    struct wl_event_source *initial_configure_idle;

    bool maximized;
    bool fullscreen;
    bool minimized;
    bool mapped;
    struct wlr_box saved_geo; // saved geo to restore when exiting maximized state

    struct wl_list link;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener request_minimize;
};

// once again see view.c
void view_new(struct wl_listener *listener, void *data);
void view_minimize(struct steppewm_view *view, bool minimized);
void view_toggle_maximize(struct steppewm_view *view);
void view_focus(struct steppewm_view *view, struct wlr_surface *surface);
void view_focus_next(struct steppewm_server *server, struct steppewm_view *skip);
struct steppewm_view *view_at(struct steppewm_server *server, double lx, double ly,
                              struct wlr_surface **surface, double *sx, double *sy);
