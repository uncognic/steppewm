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

#include <cairo/cairo.h>
#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include "input.h"
#include "paint.h"
#include "server.h"
#include "view.h"

using namespace steppewm;

// render decoration window title
void view::deco_render_title(struct wlr_scene_buffer* scene_buf, const char* text, const int w,
                             const int h, float fg[4]) {
    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

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

    canvas.commit(scene_buf);
}

// called when a new view is created
void view::deco_create() {
    const config* cfg = &srv->cfg;

    window_decoration.titlebar =
        wlr_scene_rect_create(scene_tree, 0, cfg->title_h, cfg->color_title_inactive);
    wlr_scene_node_set_position(&window_decoration.titlebar->node, 0, 0);

    window_decoration.title_label = wlr_scene_buffer_create(scene_tree, nullptr);
    wlr_scene_node_set_enabled(&window_decoration.title_label->node, cfg->show_title_text);

    window_decoration.close_button = wlr_scene_rect_create(scene_tree, cfg->close_button_w,
                                                           cfg->title_h, cfg->color_close_inactive);
    window_decoration.maximize =
        wlr_scene_rect_create(scene_tree, cfg->maximize_button_w, cfg->title_h, cfg->color_button);
    window_decoration.minimize =
        wlr_scene_rect_create(scene_tree, cfg->minimize_button_w, cfg->title_h, cfg->color_button);

    // create objects for corners and edges
    window_decoration.border_top =
        wlr_scene_rect_create(scene_tree, 0, cfg->border_w, cfg->color_invisible);
    window_decoration.border_left =
        wlr_scene_rect_create(scene_tree, cfg->border_w, 0, cfg->color_border);
    window_decoration.border_right =
        wlr_scene_rect_create(scene_tree, cfg->border_w, 0, cfg->color_border);
    window_decoration.border_bottom =
        wlr_scene_rect_create(scene_tree, 0, cfg->border_w, cfg->color_border);
    window_decoration.corner_tl =
        wlr_scene_rect_create(scene_tree, cfg->corner_size, cfg->corner_size, cfg->color_invisible);
    window_decoration.corner_tr =
        wlr_scene_rect_create(scene_tree, cfg->corner_size, cfg->corner_size, cfg->color_invisible);
    window_decoration.corner_bl =
        wlr_scene_rect_create(scene_tree, cfg->corner_size, cfg->corner_size, cfg->color_invisible);
    window_decoration.corner_br =
        wlr_scene_rect_create(scene_tree, cfg->corner_size, cfg->corner_size, cfg->color_invisible);

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

    wlr_scene_rect_set_size(window_decoration.titlebar, tw, cfg->title_h);

    wlr_scene_rect_set_size(window_decoration.close_button, cfg->close_button_w, cfg->title_h - 4);
    const int close_x = tw - cfg->close_button_w - 4;
    wlr_scene_node_set_position(&window_decoration.close_button->node, close_x, 0);

    wlr_scene_rect_set_size(window_decoration.maximize, cfg->maximize_button_w, cfg->title_h - 4);
    const int maximize_x = close_x - 4 - cfg->maximize_button_w;
    wlr_scene_node_set_position(&window_decoration.maximize->node, maximize_x, 0);

    wlr_scene_rect_set_size(window_decoration.minimize, cfg->minimize_button_w, cfg->title_h - 4);
    const int minimize_x = maximize_x - 2 - cfg->minimize_button_w;
    wlr_scene_node_set_position(&window_decoration.minimize->node, minimize_x, 0);

    if (window_decoration.title_label) {
        // render the titlebar text
        wlr_scene_node_set_enabled(&window_decoration.title_label->node, cfg->show_title_text);
        if (cfg->show_title_text) {
            const int label_w = minimize_x - 4;
            const char* title = toplevel->title ? toplevel->title : "";
            deco_render_title(window_decoration.title_label, title, label_w, cfg->title_h,
                              cfg->color_title_text);
            wlr_scene_node_set_position(&window_decoration.title_label->node, 0, 0);
        }
    }

    wlr_scene_rect_set_size(window_decoration.border_top, tw, cfg->border_w);
    wlr_scene_node_set_position(&window_decoration.border_top->node, 0, 0);

    wlr_scene_rect_set_size(window_decoration.border_left, cfg->border_w, sh);
    wlr_scene_node_set_position(&window_decoration.border_left->node, 0, cfg->title_h);

    wlr_scene_rect_set_size(window_decoration.border_right, cfg->border_w, sh);
    wlr_scene_node_set_position(&window_decoration.border_right->node, tw - cfg->border_w,
                                cfg->title_h);

    wlr_scene_rect_set_size(window_decoration.border_bottom, tw, cfg->border_w);
    wlr_scene_node_set_position(&window_decoration.border_bottom->node, 0, cfg->title_h + sh);

    wlr_scene_node_set_position(&window_decoration.corner_tl->node, 0, 0);
    wlr_scene_node_set_position(&window_decoration.corner_tr->node, tw - cfg->corner_size, 0);

    const int corner_y = cfg->title_h + sh + cfg->border_w - cfg->corner_size;
    wlr_scene_node_set_position(&window_decoration.corner_bl->node, 0, corner_y);
    wlr_scene_node_set_position(&window_decoration.corner_br->node, tw - cfg->corner_size,
                                corner_y);
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
    wlr_scene_node_set_enabled(&window_decoration.title_label->node,
                               visible && srv->cfg.show_title_text);
}

// called when a view is destroyed
void view::deco_destroy() {
    if (decoration_mode != deco_mode::SERVER || !window_decoration.titlebar) {
        return;
    }
    // free stuff
    window_decoration.titlebar = nullptr;
    window_decoration.title_label = nullptr;
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

static void lighten_color(const float in[4], float out[4], float amount = 0.25f) {
    for (int i = 0; i < 3; i++) {
        out[i] = in[i] + (1.0f - in[i]) * amount;
    }
    out[3] = in[3];
}

void view::deco_set_focus(const bool focused) const {
    if (decoration_mode != deco_mode::SERVER || !window_decoration.titlebar) {
        return;
    }
    const config* cfg = &srv->cfg;
    wlr_scene_rect_set_color(window_decoration.titlebar,
                             focused ? cfg->color_title_active : cfg->color_title_inactive);
    wlr_scene_rect_set_color(window_decoration.close_button,
                             focused ? cfg->color_close_active : cfg->color_close_inactive);
    wlr_scene_rect_set_color(window_decoration.maximize,
                             focused ? cfg->color_button : cfg->color_button_inactive);
    wlr_scene_rect_set_color(window_decoration.minimize,
                             focused ? cfg->color_button : cfg->color_button_inactive);
}

void view::deco_set_hover(const struct wlr_scene_node* node, const bool hovered) const {
    if (decoration_mode != deco_mode::SERVER || !window_decoration.titlebar) {
        return;
    }
    const config* cfg = &srv->cfg;
    const bool focused = srv->seat->keyboard_state.focused_surface == toplevel->base->surface;

    // set the color based on if it's active or not
    auto apply = [&](wlr_scene_rect* rect, const float* active, const float* inactive) {
        const float* base = focused ? active : inactive;
        if (hovered) {
            float bright[4];
            lighten_color(base, bright);
            wlr_scene_rect_set_color(rect, bright);
        } else {
            wlr_scene_rect_set_color(rect, base);
        }
    };

    if (node == &window_decoration.close_button->node) {
        apply(window_decoration.close_button, cfg->color_close_active, cfg->color_close_inactive);
    } else if (node == &window_decoration.maximize->node) {
        apply(window_decoration.maximize, cfg->color_button, cfg->color_button_inactive);
    } else if (node == &window_decoration.minimize->node) {
        apply(window_decoration.minimize, cfg->color_button, cfg->color_button_inactive);
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

    if (hit->type == WLR_SCENE_NODE_RECT) {
        *node = hit;
        return v;
    }

    // clicking on the title label should behave like clicking the titlebar
    if (hit->type == WLR_SCENE_NODE_BUFFER && v->decoration_mode == deco_mode::SERVER &&
        v->window_decoration.title_label && hit == &v->window_decoration.title_label->node) {
        *node = &v->window_decoration.titlebar->node;
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
