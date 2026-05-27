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

#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "deco.h"
#include "server.h"
#include "view.h"

// hardcoded for now hntil i can get lua working
static const float COLOR_TITLE_ACTIVE[4] = {0.24f, 0.24f, 0.24f, 1.0f};
static const float COLOR_TITLE_INACTIVE[4] = {0.14f, 0.14f, 0.14f, 1.0f};
static const float COLOR_BORDER[4] = {0.20f, 0.20f, 0.20f, 1.0f};

// called when a new view is created
void deco_create(struct steppewm_view *view) {
    view->deco.titlebar =
        wlr_scene_rect_create(view->scene_tree, 0, STEPPEWM_TITLE_H, COLOR_TITLE_INACTIVE);
    wlr_scene_node_set_position(&view->deco.titlebar->node, 0, 0);

    view->deco.border_left =
        wlr_scene_rect_create(view->scene_tree, STEPPEWM_BORDER_W, 0, COLOR_BORDER);
    view->deco.border_right =
        wlr_scene_rect_create(view->scene_tree, STEPPEWM_BORDER_W, 0, COLOR_BORDER);
    view->deco.border_bottom =
        wlr_scene_rect_create(view->scene_tree, 0, STEPPEWM_BORDER_W, COLOR_BORDER);
    view->deco.corner_bl = wlr_scene_rect_create(view->scene_tree,
        STEPPEWM_CORNER_SIZE, STEPPEWM_CORNER_SIZE, COLOR_BORDER);
    view->deco.corner_br = wlr_scene_rect_create(view->scene_tree,
        STEPPEWM_CORNER_SIZE, STEPPEWM_CORNER_SIZE, COLOR_BORDER);
}

// called when the decoration needs to be updated
void deco_update(struct steppewm_view *view) {
    if (view->deco_mode != STEPPEWM_DECO_SERVER || !view->deco.titlebar) {
        return;
    }

    int sw = view->toplevel->base->geometry.width;
    int sh = view->toplevel->base->geometry.height;
    int tw = sw + 2 * STEPPEWM_BORDER_W; // total decorated width

    wlr_scene_rect_set_size(view->deco.titlebar, tw, STEPPEWM_TITLE_H);

    wlr_scene_rect_set_size(view->deco.border_left, STEPPEWM_BORDER_W, sh);
    wlr_scene_node_set_position(&view->deco.border_left->node, 0, STEPPEWM_TITLE_H);

    wlr_scene_rect_set_size(view->deco.border_right, STEPPEWM_BORDER_W, sh);
    wlr_scene_node_set_position(&view->deco.border_right->node, tw - STEPPEWM_BORDER_W,
                                STEPPEWM_TITLE_H);

    wlr_scene_rect_set_size(view->deco.border_bottom, tw, STEPPEWM_BORDER_W);
    wlr_scene_node_set_position(&view->deco.border_bottom->node, 0, STEPPEWM_TITLE_H + sh);

    int corner_y = STEPPEWM_TITLE_H + sh + STEPPEWM_BORDER_W - STEPPEWM_CORNER_SIZE;
    wlr_scene_node_set_position(&view->deco.corner_bl->node, 0, corner_y);
    wlr_scene_node_set_position(&view->deco.corner_br->node, tw - STEPPEWM_CORNER_SIZE, corner_y);
}

// called when a view is destroyed
void deco_destroy(struct steppewm_view *view) {
    if (view->deco_mode != STEPPEWM_DECO_SERVER || !view->deco.titlebar) {
        return;
    }
    view->deco.titlebar = NULL;
    view->deco.border_left = NULL;
    view->deco.border_right = NULL;
    view->deco.border_bottom = NULL;
    view->deco.corner_bl = NULL;
    view->deco.corner_br = NULL;
}

void deco_set_focus(struct steppewm_view *view, bool focused) {
    if (view->deco_mode != STEPPEWM_DECO_SERVER || !view->deco.titlebar) {
        return;
    }
    wlr_scene_rect_set_color(view->deco.titlebar,
                             focused ? COLOR_TITLE_ACTIVE : COLOR_TITLE_INACTIVE);
}

// called when a new xdg toplevel is created
static void deco_request_mode(struct wl_listener *listener, void *data) {
    // get the steppewm_view from the listener
    struct steppewm_view *view = wl_container_of(listener, view, request_deco_mode);

    // case where the view is pending
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;

    // if the top leel surface hasnt been init yet
    if (!decoration->toplevel->base->initialized) {
        view->pending_deco = decoration;
        return;
    }

    // force server side decorations, apps are too stupid to do it right
    // im looking at you, electron
    wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
                                            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

// called when a new xdg toplevel is created
void deco_new(struct wl_listener *listener, void *data) {
    // same as the function above
    struct steppewm_server *server = wl_container_of(listener, server, new_deco);
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;

    // get the steppewm_view from the decoration
    struct steppewm_view *view = decoration->toplevel->base->data;
    if (!view) {
        return;
    }

    // defer set_mode until it can recieve configure events
    view->pending_deco = decoration;

    view->request_deco_mode.notify = deco_request_mode;
    wl_signal_add(&decoration->events.request_mode, &view->request_deco_mode);
}
