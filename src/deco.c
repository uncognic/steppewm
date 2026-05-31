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

#include <cairo/cairo.h>
#include <drm_fourcc.h>
#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include "deco.h"
#include "input.h"
#include "server.h"
#include "view.h"

// cairo stuff!
// see taskbar.c for descriptions on this stuff
struct cpu_buf {
    struct wlr_buffer base;
    uint8_t *pixels;
    size_t stride;
};

static void cpu_buf_destroy(struct wlr_buffer *wlr_buf) {
    struct cpu_buf *buf = wl_container_of(wlr_buf, buf, base);
    free(buf->pixels);
    free(buf);
}

static bool cpu_buf_begin_data_ptr_access(struct wlr_buffer *wlr_buf, uint32_t flags, void **data,
                                          uint32_t *format, size_t *stride) {
    (void) flags;
    struct cpu_buf *buf = wl_container_of(wlr_buf, buf, base);
    *data = buf->pixels;
    *format = DRM_FORMAT_ARGB8888;
    *stride = buf->stride;
    return true;
}

static void cpu_buf_end_data_ptr_access(struct wlr_buffer *wlr_buf) {
    (void) wlr_buf;
}

static const struct wlr_buffer_impl cpu_buf_impl = {
    .destroy = cpu_buf_destroy,
    .begin_data_ptr_access = cpu_buf_begin_data_ptr_access,
    .end_data_ptr_access = cpu_buf_end_data_ptr_access,
};

static struct cpu_buf *cpu_buf_create(int w, int h) {
    struct cpu_buf *buf = calloc(1, sizeof(*buf));
    buf->stride = (size_t) w * 4;
    buf->pixels = calloc(h, buf->stride);
    wlr_buffer_init(&buf->base, &cpu_buf_impl, w, h);
    return buf;
}

// render decoration window title
static void deco_render_title(struct wlr_scene_buffer *scene_buf, const char *text, int w, int h,
                              float fg[4]) {
    if (w <= 0 || h <= 0) {
        return;
    }

    struct cpu_buf *buf = cpu_buf_create(w, h);
    cairo_surface_t *surf = cairo_image_surface_create_for_data(buf->pixels, CAIRO_FORMAT_ARGB32, w,
                                                                h, (int) buf->stride);
    cairo_t *cr = cairo_create(surf);

    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);

    if (text && text[0]) {
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, h * 0.55);
        cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, text, &ext);
        double tx = 8.0 - ext.x_bearing;
        double ty = h / 2.0 - ext.y_bearing - ext.height / 2.0;
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, text);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    wlr_scene_buffer_set_buffer(scene_buf, &buf->base);
    wlr_buffer_drop(&buf->base);
}

// remove wl_listener
static void remove_listener(struct wl_listener *listener) {
    if (listener->link.next && listener->link.next != &listener->link) {
        wl_list_remove(&listener->link);
    }
    wl_list_init(&listener->link);
}

// called when a new view is created
void deco_create(struct steppewm_view *view) {
    struct steppewm_config *cfg = &view->server->config;

    view->deco.titlebar =
        wlr_scene_rect_create(view->scene_tree, 0, cfg->title_h, cfg->color_title_inactive);
    wlr_scene_node_set_position(&view->deco.titlebar->node, 0, 0);

    view->deco.title_label = wlr_scene_buffer_create(view->scene_tree, nullptr);
    wlr_scene_node_set_enabled(&view->deco.title_label->node, cfg->show_title_text);

    view->deco.close_button = wlr_scene_rect_create(view->scene_tree, cfg->close_button_w,
                                                    cfg->title_h, cfg->color_close_inactive);
    view->deco.maximize = wlr_scene_rect_create(view->scene_tree, cfg->maximize_button_w,
                                                cfg->title_h, cfg->color_button);
    view->deco.minimize = wlr_scene_rect_create(view->scene_tree, cfg->minimize_button_w,
                                                cfg->title_h, cfg->color_button);

    // create objects for corners and edges
    view->deco.border_top =
        wlr_scene_rect_create(view->scene_tree, 0, cfg->border_w, cfg->color_invisible);
    view->deco.border_left =
        wlr_scene_rect_create(view->scene_tree, cfg->border_w, 0, cfg->color_border);
    view->deco.border_right =
        wlr_scene_rect_create(view->scene_tree, cfg->border_w, 0, cfg->color_border);
    view->deco.border_bottom =
        wlr_scene_rect_create(view->scene_tree, 0, cfg->border_w, cfg->color_border);
    view->deco.corner_tl = wlr_scene_rect_create(view->scene_tree, cfg->corner_size,
                                                 cfg->corner_size, cfg->color_invisible);
    view->deco.corner_tr = wlr_scene_rect_create(view->scene_tree, cfg->corner_size,
                                                 cfg->corner_size, cfg->color_invisible);
    view->deco.corner_bl = wlr_scene_rect_create(view->scene_tree, cfg->corner_size,
                                                 cfg->corner_size, cfg->color_invisible);
    view->deco.corner_br = wlr_scene_rect_create(view->scene_tree, cfg->corner_size,
                                                 cfg->corner_size, cfg->color_invisible);

    deco_update(view);
}

// called when the decoration needs to be updated
void deco_update(struct steppewm_view *view) {
    if (view->deco_mode != STEPPEWM_DECO_SERVER || !view->deco.titlebar) {
        return;
    }

    struct steppewm_config *cfg = &view->server->config;
    int sw = view->toplevel->base->geometry.width;
    int sh = view->toplevel->base->geometry.height;
    int tw = sw + 2 * cfg->border_w;

    wlr_scene_rect_set_size(view->deco.titlebar, tw, cfg->title_h);

    wlr_scene_rect_set_size(view->deco.close_button, cfg->close_button_w, cfg->title_h - 4);
    int close_x = tw - cfg->close_button_w - 4;
    wlr_scene_node_set_position(&view->deco.close_button->node, close_x, 0);

    wlr_scene_rect_set_size(view->deco.maximize, cfg->maximize_button_w, cfg->title_h - 4);
    int maximize_x = close_x - 4 - cfg->maximize_button_w;
    wlr_scene_node_set_position(&view->deco.maximize->node, maximize_x, 0);

    wlr_scene_rect_set_size(view->deco.minimize, cfg->minimize_button_w, cfg->title_h - 4);
    int minimize_x = maximize_x - 2 - cfg->minimize_button_w;
    wlr_scene_node_set_position(&view->deco.minimize->node, minimize_x, 0);

    if (view->deco.title_label) {
        // render the titlebar text
        wlr_scene_node_set_enabled(&view->deco.title_label->node, cfg->show_title_text);
        if (cfg->show_title_text) {
            int label_w = minimize_x - 4;
            const char *title = view->toplevel->title ? view->toplevel->title : "";
            deco_render_title(view->deco.title_label, title, label_w, cfg->title_h,
                              cfg->color_title_text);
            wlr_scene_node_set_position(&view->deco.title_label->node, 0, 0);
        }
    }

    wlr_scene_rect_set_size(view->deco.border_top, tw, cfg->border_w);
    wlr_scene_node_set_position(&view->deco.border_top->node, 0, 0);

    wlr_scene_rect_set_size(view->deco.border_left, cfg->border_w, sh);
    wlr_scene_node_set_position(&view->deco.border_left->node, 0, cfg->title_h);

    wlr_scene_rect_set_size(view->deco.border_right, cfg->border_w, sh);
    wlr_scene_node_set_position(&view->deco.border_right->node, tw - cfg->border_w, cfg->title_h);

    wlr_scene_rect_set_size(view->deco.border_bottom, tw, cfg->border_w);
    wlr_scene_node_set_position(&view->deco.border_bottom->node, 0, cfg->title_h + sh);

    wlr_scene_node_set_position(&view->deco.corner_tl->node, 0, 0);
    wlr_scene_node_set_position(&view->deco.corner_tr->node, tw - cfg->corner_size, 0);

    int corner_y = cfg->title_h + sh + cfg->border_w - cfg->corner_size;
    wlr_scene_node_set_position(&view->deco.corner_bl->node, 0, corner_y);
    wlr_scene_node_set_position(&view->deco.corner_br->node, tw - cfg->corner_size, corner_y);
}

// called when a view is destroyed
void deco_destroy(struct steppewm_view *view) {
    if (view->deco_mode != STEPPEWM_DECO_SERVER || !view->deco.titlebar) {
        return;
    }
    // free stuff
    view->deco.titlebar = nullptr;
    view->deco.title_label = nullptr;
    view->deco.close_button = nullptr;
    view->deco.maximize = nullptr;
    view->deco.minimize = nullptr;
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
    struct steppewm_config *cfg = &view->server->config;
    wlr_scene_rect_set_color(view->deco.titlebar,
                             focused ? cfg->color_title_active : cfg->color_title_inactive);
    wlr_scene_rect_set_color(view->deco.close_button,
                             focused ? cfg->color_close_active : cfg->color_close_inactive);
    wlr_scene_rect_set_color(view->deco.maximize,
                             focused ? cfg->color_button : cfg->color_button_inactive);
    wlr_scene_rect_set_color(view->deco.minimize,
                             focused ? cfg->color_button : cfg->color_button_inactive);
}

const char *deco_cursor_name(struct steppewm_view *view, struct wlr_scene_node *node) {
    if (!view || view->deco_mode != STEPPEWM_DECO_SERVER) {
        return nullptr;
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

    return nullptr;
}

struct steppewm_view *deco_at(struct steppewm_server *server, double lx, double ly,
                              struct wlr_scene_node **node) {
    double sx, sy;
    struct wlr_scene_node *hit = wlr_scene_node_at(&server->scene->tree.node, lx, ly, &sx, &sy);
    if (!hit) {
        return nullptr;
    }

    struct wlr_scene_tree *tree = hit->parent;
    while (tree && !tree->node.data) {
        tree = tree->node.parent;
    }
    if (!tree) {
        return nullptr;
    }
    struct steppewm_view *view = tree->node.data;

    if (hit->type == WLR_SCENE_NODE_RECT) {
        *node = hit;
        return view;
    }

    // clicking on the title label should behave like clicking the titlebar
    if (hit->type == WLR_SCENE_NODE_BUFFER && view->deco_mode == STEPPEWM_DECO_SERVER &&
        view->deco.title_label && hit == &view->deco.title_label->node) {
        *node = &view->deco.titlebar->node;
        return view;
    }

    return nullptr;
}

bool deco_handle_button(struct steppewm_view *view, struct steppewm_server *server, struct wlr_scene_node *node, uint32_t button) {
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

    // handle maximize
    if (node == &view->deco.maximize->node) {
        if (button == BTN_LEFT) {
            view_toggle_maximize(view);
        }
        return true;
    }

    // handle minimize
    if (node == &view->deco.minimize->node) {
        if (button == BTN_LEFT) {
            view_minimize(view, true);
            view_focus_next(server, view);
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
    (void) data;

    view->pending_deco = nullptr;
    view->decoration = nullptr;

    remove_listener(&view->request_deco_mode);
    remove_listener(&view->destroy_deco);
}

// called when a new xdg toplevel is created
void deco_new(struct wl_listener *listener, void *data) {
    (void) listener;
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
