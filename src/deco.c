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

#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include "deco.h"
#include "input.h"
#include "server.h"
#include "view.h"

// hardcoded for now hntil i can get lua working
static const float COLOR_TITLE_ACTIVE[4] = {0.24f, 0.24f, 0.24f, 1.0f};
static const float COLOR_TITLE_INACTIVE[4] = {0.14f, 0.14f, 0.14f, 1.0f};
static const float COLOR_BORDER[4] = {0.20f, 0.20f, 0.20f, 1.0f};
static const float COLOR_CLOSE_ACTIVE[4] = {0.85f, 0.08f, 0.08f, 1.0f};
static const float COLOR_CLOSE_INACTIVE[4] = {0.45f, 0.06f, 0.06f, 1.0f};
static const float COLOR_INVISIBLE[4] = {0.0f, 0.0f, 0.0f, 0.0f};

// remove wl_listener
static void remove_listener(struct wl_listener *listener) {
    if (listener->link.next && listener->link.next != &listener->link) {
        wl_list_remove(&listener->link);
    }
    wl_list_init(&listener->link);
}

// called when a new view is created
void deco_create(struct steppewm_view *view) {
    view->deco.titlebar =
        wlr_scene_rect_create(view->scene_tree, 0, STEPPEWM_TITLE_H, COLOR_TITLE_INACTIVE);
    wlr_scene_node_set_position(&view->deco.titlebar->node, 0, 0);

    // create rectangle for close button
    view->deco.close_button = wlr_scene_rect_create(view->scene_tree, STEPPEWM_CLOSE_BUTTON_W,
                                                    STEPPEWM_TITLE_H, COLOR_CLOSE_INACTIVE);

    // create objects for corners and edges
    view->deco.border_top =
        wlr_scene_rect_create(view->scene_tree, 0, STEPPEWM_BORDER_W, COLOR_INVISIBLE);
    view->deco.border_left =
        wlr_scene_rect_create(view->scene_tree, STEPPEWM_BORDER_W, 0, COLOR_BORDER);
    view->deco.border_right =
        wlr_scene_rect_create(view->scene_tree, STEPPEWM_BORDER_W, 0, COLOR_BORDER);
    view->deco.border_bottom =
        wlr_scene_rect_create(view->scene_tree, 0, STEPPEWM_BORDER_W, COLOR_BORDER);
    view->deco.corner_tl = wlr_scene_rect_create(view->scene_tree,
        STEPPEWM_CORNER_SIZE, STEPPEWM_CORNER_SIZE, COLOR_INVISIBLE);
    view->deco.corner_tr = wlr_scene_rect_create(view->scene_tree,
        STEPPEWM_CORNER_SIZE, STEPPEWM_CORNER_SIZE, COLOR_INVISIBLE);
    view->deco.corner_bl = wlr_scene_rect_create(view->scene_tree,
        STEPPEWM_CORNER_SIZE, STEPPEWM_CORNER_SIZE, COLOR_INVISIBLE);
    view->deco.corner_br = wlr_scene_rect_create(view->scene_tree,
        STEPPEWM_CORNER_SIZE, STEPPEWM_CORNER_SIZE, COLOR_INVISIBLE);

    deco_update(view);
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

    // set size and position of the close button
    wlr_scene_rect_set_size(view->deco.close_button, STEPPEWM_CLOSE_BUTTON_W, STEPPEWM_TITLE_H);
    wlr_scene_node_set_position(&view->deco.close_button->node, tw - STEPPEWM_CLOSE_BUTTON_W, 0);

    // set sizes and positions for the corners and edges
    wlr_scene_rect_set_size(view->deco.border_top, tw, STEPPEWM_BORDER_W);
    wlr_scene_node_set_position(&view->deco.border_top->node, 0, 0);

    wlr_scene_rect_set_size(view->deco.border_left, STEPPEWM_BORDER_W, sh);
    wlr_scene_node_set_position(&view->deco.border_left->node, 0, STEPPEWM_TITLE_H);

    wlr_scene_rect_set_size(view->deco.border_right, STEPPEWM_BORDER_W, sh);
    wlr_scene_node_set_position(&view->deco.border_right->node, tw - STEPPEWM_BORDER_W,
                                STEPPEWM_TITLE_H);

    wlr_scene_rect_set_size(view->deco.border_bottom, tw, STEPPEWM_BORDER_W);
    wlr_scene_node_set_position(&view->deco.border_bottom->node, 0, STEPPEWM_TITLE_H + sh);

    wlr_scene_node_set_position(&view->deco.corner_tl->node, 0, 0);
    wlr_scene_node_set_position(&view->deco.corner_tr->node, tw - STEPPEWM_CORNER_SIZE, 0);

    int corner_y = STEPPEWM_TITLE_H + sh + STEPPEWM_BORDER_W - STEPPEWM_CORNER_SIZE;
    wlr_scene_node_set_position(&view->deco.corner_bl->node, 0, corner_y);
    wlr_scene_node_set_position(&view->deco.corner_br->node, tw - STEPPEWM_CORNER_SIZE, corner_y);
}

// called when a view is destroyed
void deco_destroy(struct steppewm_view *view) {
    if (view->deco_mode != STEPPEWM_DECO_SERVER || !view->deco.titlebar) {
        return;
    }
    // free stuff
    view->deco.titlebar = nullptr;
    view->deco.close_button = nullptr;
    view->deco.border_top = nullptr;
    view->deco.border_left = nullptr;
    view->deco.border_right = nullptr;
    view->deco.border_bottom = nullptr;
    view->deco.corner_tl = nullptr;
    view->deco.corner_tr = nullptr;
    view->deco.corner_bl = nullptr;
    view->deco.corner_br = nullptr;
}

void deco_set_focus(struct steppewm_view *view, bool focused) {
    if (!view || view->deco_mode != STEPPEWM_DECO_SERVER || !view->deco.titlebar) {
        return;
    }
    wlr_scene_rect_set_color(view->deco.titlebar,
                             focused ? COLOR_TITLE_ACTIVE : COLOR_TITLE_INACTIVE);
    wlr_scene_rect_set_color(view->deco.close_button,
                             focused ? COLOR_CLOSE_ACTIVE : COLOR_CLOSE_INACTIVE);
}



const char *deco_cursor_name(struct steppewm_view *view, struct wlr_scene_node *node) {
    if (!view || view->deco_mode != STEPPEWM_DECO_SERVER) {
        return NULL;
    }

    // set cursor when in a resize area
    if (node == &view->deco.corner_tl->node) {
        return "nw-resize";
    }
    if (node == &view->deco.corner_tr->node) {
        return "ne-resize";
    }
    if (node == &view->deco.corner_bl->node) {
        return "sw-resize";
    }
    if (node == &view->deco.corner_br->node) {
        return "se-resize";
    }
    if (node == &view->deco.border_left->node) {
        return "w-resize";
    }
    if (node == &view->deco.border_right->node) {
        return "e-resize";
    }
    if (node == &view->deco.border_top->node) {
        return "n-resize";
    }
    if (node == &view->deco.border_bottom->node) {
        return "s-resize";
    }

    return NULL;
}

struct steppewm_view *deco_at(struct steppewm_server *server, double lx, double ly,
                              struct wlr_scene_node **node) {
    double sx, sy;
    struct wlr_scene_node *hit =
        wlr_scene_node_at(&server->scene->tree.node, lx, ly, &sx, &sy);
    if (!hit || hit->type != WLR_SCENE_NODE_RECT) {
        return NULL;
    }

    struct wlr_scene_tree *tree = hit->parent;
    while (tree && !tree->node.data) {
        tree = tree->node.parent;
    }
    if (!tree) {
        return NULL;
    }

    *node = hit;
    return tree->node.data;
}

bool deco_handle_button(struct steppewm_view *view, struct wlr_scene_node *node, uint32_t button) {
    if (!view || view->deco_mode != STEPPEWM_DECO_SERVER) {
        return false;
    }

    // handle close
    if (node == &view->deco.close_button->node) {
        if (button == BTN_LEFT) {
            wlr_xdg_toplevel_send_close(view->toplevel);
        }
        return true;
    }

    // handle resizing
    if (node == &view->deco.titlebar->node) {
        cursor_begin_interactive(view, STEPPEWM_CURSOR_MOVE, 0);
        return true;
    }
    if (node == &view->deco.corner_tl->node) {
        cursor_begin_interactive(view, STEPPEWM_CURSOR_RESIZE, WLR_EDGE_TOP | WLR_EDGE_LEFT);
        return true;
    }
    if (node == &view->deco.corner_tr->node) {
        cursor_begin_interactive(view, STEPPEWM_CURSOR_RESIZE, WLR_EDGE_TOP | WLR_EDGE_RIGHT);
        return true;
    }
    if (node == &view->deco.corner_bl->node) {
        cursor_begin_interactive(view, STEPPEWM_CURSOR_RESIZE, WLR_EDGE_BOTTOM | WLR_EDGE_LEFT);
        return true;
    }
    if (node == &view->deco.corner_br->node) {
        cursor_begin_interactive(view, STEPPEWM_CURSOR_RESIZE, WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
        return true;
    }
    if (node == &view->deco.border_left->node) {
        cursor_begin_interactive(view, STEPPEWM_CURSOR_RESIZE, WLR_EDGE_LEFT);
        return true;
    }
    if (node == &view->deco.border_right->node) {
        cursor_begin_interactive(view, STEPPEWM_CURSOR_RESIZE, WLR_EDGE_RIGHT);
        return true;
    }
    if (node == &view->deco.border_top->node) {
        cursor_begin_interactive(view, STEPPEWM_CURSOR_RESIZE, WLR_EDGE_TOP);
        return true;
    }
    if (node == &view->deco.border_bottom->node) {
        cursor_begin_interactive(view, STEPPEWM_CURSOR_RESIZE, WLR_EDGE_BOTTOM);
        return true;
    }

    return false;
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

// destroy decoration handle
static void deco_handle_destroy(struct wl_listener *listener, void *data) {
    struct steppewm_view *view = wl_container_of(listener, view, destroy_deco);
    (void)data;

    view->pending_deco = nullptr;
    view->decoration = nullptr;

    remove_listener(&view->request_deco_mode);
    remove_listener(&view->destroy_deco);
}

// called when a new xdg toplevel is created
void deco_new(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;

    // get the steppewm_view from the decoration
    struct steppewm_view *view = decoration->toplevel->base->data;
    if (!view) {
        return;
    }

    // defer set_mode until it can recieve configure events
    view->decoration = decoration;
    view->pending_deco = decoration;

    view->request_deco_mode.notify = deco_request_mode;
    wl_signal_add(&decoration->events.request_mode, &view->request_deco_mode);

    view->destroy_deco.notify = deco_handle_destroy;
    wl_signal_add(&decoration->events.destroy, &view->destroy_deco);
}
