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
    struct wlr_scene_rect *border_left;
    struct wlr_scene_rect *border_right;
    struct wlr_scene_rect *border_bottom;
};

enum steppewm_deco_mode {
    STEPPEWM_DECO_SERVER,
    STEPPEWM_DECO_CLIENT,
};

struct steppewm_view {
    struct steppewm_server *server;
    struct wlr_xdg_toplevel *toplevel;
    struct wlr_scene_tree *scene_tree; // container: whole decorated window
    struct wlr_scene_tree *xdg_tree;   // content: the actual content w/o titlebar and borders
    enum steppewm_deco_mode deco_mode;

    struct steppewm_deco deco;                           // the decoration
    struct wlr_xdg_toplevel_decoration_v1 *pending_deco; // applied once configure events are legal
    struct wl_listener request_deco_mode;                // for xdg-decoration request_mode
    struct wl_event_source *initial_configure_idle;

    bool maximized;
    bool fullscreen;
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
};

// once again see view.c
void view_new(struct wl_listener *listener, void *data);
void view_focus(struct steppewm_view *view, struct wlr_surface *surface);
struct steppewm_view *view_at(struct steppewm_server *server, double lx, double ly,
                              struct wlr_surface **surface, double *sx, double *sy);
