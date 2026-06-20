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

#include "wlr.h"

#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include "input.h"
#include "paint.h"
#include "server.h"
#include "theme.h"
#include "view.h"

using namespace steppewm;

static constexpr float invisible[] = {0.0f, 0.0f, 0.0f, 0.0f};

void view::deco_render_button(struct wlr_scene_buffer *buf, int type, const bool focused,
                              const bool hovered) const {
    const config *cfg = &srv->cfg;
    int w, h;
    const float *active;
    const float *inactive;
    const float *hover_color;

    if (type == theme::BTN_CLOSE) {
        w = cfg->close_button_w;
        active = cfg->color_close_active;
        inactive = cfg->color_close_inactive;
        hover_color = cfg->color_close_hover;
    } else if (type == theme::BTN_MAXIMIZE) {
        w = cfg->maximize_button_w;
        active = cfg->color_maximize_active;
        inactive = cfg->color_maximize_inactive;
        hover_color = cfg->color_maximize_hover;
    } else {
        w = cfg->minimize_button_w;
        active = cfg->color_minimize_active;
        inactive = cfg->color_minimize_inactive;
        hover_color = cfg->color_minimize_hover;
    }
    h = cfg->title_h - 4;
    if (w <= 0 || h <= 0) {
        return;
    }

    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    srv->wm_theme.paint_button(canvas.cr(), w, h, type, focused, hovered, active, inactive,
                               cfg->color_title_text, cfg->button_style, hover_color);
    canvas.commit(buf);
}

// render all themed decoration buffers for the current state
void view::deco_render_all() const {
    if (decoration_mode != deco_mode::SERVER || !window_decoration.titlebar || fullscreen) {
        return;
    }

    const config *cfg = &srv->cfg;
    const bool focused = srv->seat->keyboard_state.focused_surface == toplevel->base->surface;
    const int sw = toplevel->base->geometry.width;
    const int tw = sw + 2 * cfg->border_w;
    const int sh = toplevel->base->geometry.height;

    // titlebar
    if (tw > 0 && cfg->title_h > 0) {
        const int buttons_w = cfg->close_button_w + cfg->maximize_button_w + cfg->minimize_button_w +
                              12;
        const char *title = toplevel->title ? toplevel->title : "";
        paint::Canvas canvas(tw, cfg->title_h);
        if (canvas.valid()) {
            srv->wm_theme.paint_titlebar(canvas.cr(), tw, cfg->title_h, focused, title,
                                         cfg->color_title_active, cfg->color_title_inactive,
                                         cfg->color_title_text, buttons_w,
                                         cfg->title_gradient, cfg->font, cfg->title_font_size,
                                         cfg->center_title_text, cfg->buttons_left);
            canvas.commit(window_decoration.titlebar);
        }
    }

    // buttons
    deco_render_button(window_decoration.close_button, theme::BTN_CLOSE, focused, false);
    deco_render_button(window_decoration.maximize, theme::BTN_MAXIMIZE, focused, false);
    deco_render_button(window_decoration.minimize, theme::BTN_MINIMIZE, focused, false);

    // borders
    const float *border_color = focused ? cfg->color_border_active : cfg->color_border_inactive;
    if (cfg->border_w > 0 && sh > 0) {
        {
            paint::Canvas canvas(cfg->border_w, sh);
            if (canvas.valid()) {
                srv->wm_theme.paint_border(canvas.cr(), cfg->border_w, sh, border_color,
                                           theme::EDGE_LEFT, cfg->border_style);
                canvas.commit(window_decoration.border_left);
            }
        }
        {
            paint::Canvas canvas(cfg->border_w, sh);
            if (canvas.valid()) {
                srv->wm_theme.paint_border(canvas.cr(), cfg->border_w, sh, border_color,
                                           theme::EDGE_RIGHT, cfg->border_style);
                canvas.commit(window_decoration.border_right);
            }
        }
    }
    if (cfg->border_w > 0 && tw > 0) {
        paint::Canvas canvas(tw, cfg->border_w);
        if (canvas.valid()) {
            srv->wm_theme.paint_border(canvas.cr(), tw, cfg->border_w, border_color,
                                       theme::EDGE_BOTTOM, cfg->border_style);
            canvas.commit(window_decoration.border_bottom);
        }
    }
}

// called when a new view is created
void view::deco_create() {
    const config* cfg = &srv->cfg;

    // visual elements (buffers)
    window_decoration.titlebar = wlr_scene_buffer_create(scene_tree, nullptr);
    wlr_scene_node_set_position(&window_decoration.titlebar->node, 0, 0);

    window_decoration.close_button = wlr_scene_buffer_create(scene_tree, nullptr);
    window_decoration.maximize = wlr_scene_buffer_create(scene_tree, nullptr);
    window_decoration.minimize = wlr_scene_buffer_create(scene_tree, nullptr);

    window_decoration.border_left = wlr_scene_buffer_create(scene_tree, nullptr);
    window_decoration.border_right = wlr_scene_buffer_create(scene_tree, nullptr);
    window_decoration.border_bottom = wlr_scene_buffer_create(scene_tree, nullptr);

    // invisible hit areas for resize (rects)
    window_decoration.border_top =
            wlr_scene_rect_create(scene_tree, 0, cfg->border_w, invisible);
    window_decoration.corner_tl =
            wlr_scene_rect_create(scene_tree, cfg->corner_size, cfg->corner_size, invisible);
    window_decoration.corner_tr =
            wlr_scene_rect_create(scene_tree, cfg->corner_size, cfg->corner_size, invisible);
    window_decoration.corner_bl =
            wlr_scene_rect_create(scene_tree, cfg->corner_size, cfg->corner_size, invisible);
    window_decoration.corner_br =
            wlr_scene_rect_create(scene_tree, cfg->corner_size, cfg->corner_size, invisible);

    deco_update();
}

// called when the decoration needs to be updated
void view::deco_update() const {
    if (decoration_mode != deco_mode::SERVER || !window_decoration.titlebar || fullscreen) {
        return;
    }

    config* cfg = &srv->cfg;
    const int sw = toplevel->base->geometry.width;
    const int sh = toplevel->base->geometry.height;
    const int tw = sw + 2 * cfg->border_w;

    // position buttons within the titlebar (vertically centered)
    const int btn_h = cfg->title_h - 4;
    const int btn_y = (cfg->title_h - btn_h) / 2;
    int close_x, maximize_x, minimize_x;
    if (cfg->buttons_left) {
        close_x = 4;
        maximize_x = close_x + cfg->close_button_w + 4;
        minimize_x = maximize_x + cfg->maximize_button_w + 2;
    } else {
        close_x = tw - cfg->close_button_w - 4;
        maximize_x = close_x - 4 - cfg->maximize_button_w;
        minimize_x = maximize_x - 2 - cfg->minimize_button_w;
    }
    wlr_scene_node_set_position(&window_decoration.close_button->node, close_x, btn_y);
    wlr_scene_node_set_position(&window_decoration.maximize->node, maximize_x, btn_y);
    wlr_scene_node_set_position(&window_decoration.minimize->node, minimize_x, btn_y);

    // position borders
    wlr_scene_node_set_position(&window_decoration.border_left->node, 0, cfg->title_h);
    wlr_scene_node_set_position(&window_decoration.border_right->node, tw - cfg->border_w,
                                cfg->title_h);
    wlr_scene_node_set_position(&window_decoration.border_bottom->node, 0, cfg->title_h + sh);

    // position invisible hit areas
    wlr_scene_rect_set_size(window_decoration.border_top, tw, cfg->border_w);
    wlr_scene_node_set_position(&window_decoration.border_top->node, 0, 0);

    wlr_scene_node_set_position(&window_decoration.corner_tl->node, 0, 0);
    wlr_scene_node_set_position(&window_decoration.corner_tr->node, tw - cfg->corner_size, 0);

    const int corner_y = cfg->title_h + sh + cfg->border_w - cfg->corner_size;
    wlr_scene_node_set_position(&window_decoration.corner_bl->node, 0, corner_y);
    wlr_scene_node_set_position(&window_decoration.corner_br->node, tw - cfg->corner_size,
                                corner_y);

    // render all visual elements
    deco_render_all();
}

void view::deco_set_visible(bool visible) const {
    if (decoration_mode != deco_mode::SERVER || !window_decoration.titlebar) {
        return;
    }
    wlr_scene_node* nodes[] = {
        &window_decoration.titlebar->node,     &window_decoration.close_button->node,
        &window_decoration.maximize->node,     &window_decoration.minimize->node,
        &window_decoration.border_top->node,   &window_decoration.border_left->node,
        &window_decoration.border_right->node, &window_decoration.border_bottom->node,
        &window_decoration.corner_tl->node,    &window_decoration.corner_tr->node,    &window_decoration.corner_bl->node,     &window_decoration.corner_br->node,
    };
    for (wlr_scene_node* n : nodes) {
        wlr_scene_node_set_enabled(n, visible);
    }
}

// called when a view is destroyed
void view::deco_destroy() {
    if (decoration_mode != deco_mode::SERVER || !window_decoration.titlebar) {
        return;
    }
    // free stuff
    window_decoration.titlebar = nullptr;
    window_decoration.close_button = nullptr;
    window_decoration.maximize = nullptr;
    window_decoration.minimize = nullptr;
    window_decoration.border_top = nullptr;
    window_decoration.border_left = nullptr;
    window_decoration.border_right = nullptr;
    window_decoration.border_bottom = nullptr;
    window_decoration.corner_tl = nullptr;
    window_decoration.corner_tr = nullptr;
    window_decoration.corner_bl = nullptr;
    window_decoration.corner_br = nullptr;
}

void view::deco_set_focus(const bool focused) const {
    if (decoration_mode != deco_mode::SERVER || !window_decoration.titlebar) {
        return;
    }

    const config* cfg = &srv->cfg;
    const int sw = toplevel->base->geometry.width;
    const int tw = sw + 2 * cfg->border_w;

    // re-render titlebar with new focus state
    if (tw > 0 && cfg->title_h > 0) {
        const int buttons_w = cfg->close_button_w + cfg->maximize_button_w + cfg->minimize_button_w +
                              12;
        const char *title = toplevel->title ? toplevel->title : "";
        paint::Canvas canvas(tw, cfg->title_h);
        if (canvas.valid()) {
            srv->wm_theme.paint_titlebar(canvas.cr(), tw, cfg->title_h, focused, title,
                                         cfg->color_title_active, cfg->color_title_inactive,
                                         cfg->color_title_text, buttons_w,
                                         cfg->title_gradient, cfg->font, cfg->title_font_size,
                                         cfg->center_title_text, cfg->buttons_left);
            canvas.commit(window_decoration.titlebar);
        }
    }

    // re-render buttons
    deco_render_button(window_decoration.close_button, theme::BTN_CLOSE, focused, false);
    deco_render_button(window_decoration.maximize, theme::BTN_MAXIMIZE, focused, false);
    deco_render_button(window_decoration.minimize, theme::BTN_MINIMIZE, focused, false);
}

void view::deco_set_hover(const struct wlr_scene_node* node, const bool hovered) const {
    if (decoration_mode != deco_mode::SERVER || !window_decoration.titlebar) {
        return;
    }
    const bool focused = srv->seat->keyboard_state.focused_surface == toplevel->base->surface;

    if (node == &window_decoration.close_button->node) {
        deco_render_button(window_decoration.close_button, theme::BTN_CLOSE, focused, hovered);
    } else if (node == &window_decoration.maximize->node) {
        deco_render_button(window_decoration.maximize, theme::BTN_MAXIMIZE, focused, hovered);
    } else if (node == &window_decoration.minimize->node) {
        deco_render_button(window_decoration.minimize, theme::BTN_MINIMIZE, focused, hovered);
    }
}

bool view::deco_is_button(const struct wlr_scene_node* node) const {
    if (decoration_mode != deco_mode::SERVER || !window_decoration.titlebar) {
        return false;
    }
    return node == &window_decoration.close_button->node ||
           node == &window_decoration.maximize->node || node == &window_decoration.minimize->node;
}

const char* view::deco_cursor_name(const struct wlr_scene_node* node) const {
    if (decoration_mode != deco_mode::SERVER) {
        return nullptr;
    }

    // set cursor when in a resize area
    if (node == &window_decoration.corner_tl->node) {
        return "nw-resize";
    }
    if (node == &window_decoration.corner_tr->node) {
        return "ne-resize";
    }
    if (node == &window_decoration.corner_bl->node) {
        return "sw-resize";
    }
    if (node == &window_decoration.corner_br->node) {
        return "se-resize";
    }
    if (node == &window_decoration.border_left->node) {
        return "w-resize";
    }
    if (node == &window_decoration.border_right->node) {
        return "e-resize";
    }
    if (node == &window_decoration.border_top->node) {
        return "n-resize";
    }
    if (node == &window_decoration.border_bottom->node) {
        return "s-resize";
    }

    return nullptr;
}

view* view::deco_at(const server* s, const double lx, const double ly,
                    struct wlr_scene_node** node) {
    double sx, sy;
    struct wlr_scene_node* hit = wlr_scene_node_at(&s->scene->tree.node, lx, ly, &sx, &sy);
    if (!hit) {
        return nullptr;
    }

    const struct wlr_scene_tree* tree = hit->parent;
    while (tree && !tree->node.data) {
        tree = tree->node.parent;
    }
    if (!tree) {
        return nullptr;
    }
    const auto v = static_cast<view*>(tree->node.data);

    if (v->decoration_mode != deco_mode::SERVER || !v->window_decoration.titlebar) {
        return nullptr;
    }

    // invisible resize rects
    if (hit->type == WLR_SCENE_NODE_RECT) {
        *node = hit;
        return v;
    }

    // themed decoration buffers
    if (hit == &v->window_decoration.titlebar->node ||
        hit == &v->window_decoration.close_button->node ||
        hit == &v->window_decoration.maximize->node ||
        hit == &v->window_decoration.minimize->node) {
        *node = hit;
        return v;
    }

    return nullptr;
}

bool view::deco_handle_button(server* s, const struct wlr_scene_node* node, const uint32_t button,
                              const uint32_t time_msec) {
    if (decoration_mode != deco_mode::SERVER) {
        return false;
    }

    // handle close
    if (node == &window_decoration.close_button->node) {
        if (button == BTN_LEFT) {
            wlr_xdg_toplevel_send_close(toplevel);
        }
        return true;
    }

    // handle maximize
    if (node == &window_decoration.maximize->node) {
        if (button == BTN_LEFT) {
            toggle_maximize();
        }
        return true;
    }

    // handle minimize
    if (node == &window_decoration.minimize->node) {
        if (button == BTN_LEFT) {
            minimize(true);
            view::focus_next(s, this);
        }
        return true;
    }
    if (node == &window_decoration.titlebar->node) {
        // if it was a double click
        if (button == BTN_LEFT && s->titlebar_last_click_view == this &&
            time_msec - s->titlebar_last_click_time < 400) {
            toggle_maximize();
            s->titlebar_last_click_time = 0;
            s->titlebar_last_click_view = nullptr;
        } else { // otherwise reset the timer
            if (button == BTN_LEFT) {
                s->titlebar_last_click_time = time_msec;
                s->titlebar_last_click_view = this;
            }
            server::cursor_begin_interactive(this, cursor_mode::move, 0);
        }
        return true;
    }
    if (node == &window_decoration.corner_tl->node) {
        server::cursor_begin_interactive(this, cursor_mode::resize, WLR_EDGE_TOP | WLR_EDGE_LEFT);
        return true;
    }
    if (node == &window_decoration.corner_tr->node) {
        server::cursor_begin_interactive(this, cursor_mode::resize, WLR_EDGE_TOP | WLR_EDGE_RIGHT);
        return true;
    }
    if (node == &window_decoration.corner_bl->node) {
        server::cursor_begin_interactive(this, cursor_mode::resize,
                                         WLR_EDGE_BOTTOM | WLR_EDGE_LEFT);
        return true;
    }
    if (node == &window_decoration.corner_br->node) {
        server::cursor_begin_interactive(this, cursor_mode::resize,
                                         WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
        return true;
    }
    if (node == &window_decoration.border_left->node) {
        server::cursor_begin_interactive(this, cursor_mode::resize, WLR_EDGE_LEFT);
        return true;
    }
    if (node == &window_decoration.border_right->node) {
        server::cursor_begin_interactive(this, cursor_mode::resize, WLR_EDGE_RIGHT);
        return true;
    }
    if (node == &window_decoration.border_top->node) {
        server::cursor_begin_interactive(this, cursor_mode::resize, WLR_EDGE_TOP);
        return true;
    }
    if (node == &window_decoration.border_bottom->node) {
        server::cursor_begin_interactive(this, cursor_mode::resize, WLR_EDGE_BOTTOM);
        return true;
    }

    return false;
}

// called when the client requests a decoration mode
void view::deco_request_mode(view* v, struct wlr_xdg_toplevel_decoration_v1* decoration) {
    // if the top leel surface hasnt been init yet
    if (!decoration->toplevel->base->initialized) {
        v->pending_deco = decoration;
        return;
    }
    // force server side decorations
    wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
                                            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

// destroy decoration handle
void view::deco_handle_destroy(view* v) {
    v->pending_deco = nullptr;
    v->decoration = nullptr;
    v->request_deco_mode.disconnect();
    v->destroy_deco.disconnect();
}

// called when a new xdg toplevel is created
void view::deco_new(struct wl_listener* listener, void* data) {
    (void) listener;
    auto* decoration = static_cast<struct wlr_xdg_toplevel_decoration_v1*>(data);

    // get the steppewm_view from the decoration
    auto v = static_cast<view*>(decoration->toplevel->base->data);
    if (!v) {
        return;
    }

    // defer set_mode until it can recieve configure events
    v->decoration = decoration;
    v->pending_deco = decoration;

    v->request_deco_mode.connect(&decoration->events.request_mode, [v](void* data) {
        deco_request_mode(v, static_cast<struct wlr_xdg_toplevel_decoration_v1*>(data));
    });
    v->destroy_deco.connect(&decoration->events.destroy, [v](void*) { deco_handle_destroy(v); });
}
