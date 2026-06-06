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

#include "wlr.hxx"

#include <cairo/cairo.h>
#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include "deco.hxx"
#include "input.hxx"
#include "paint.hxx"
#include "server.hxx"
#include "view.hxx"

using namespace steppewm;

// render decoration window title
static void deco_render_title(struct wlr_scene_buffer* scene_buf, const char* text, const int w,
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

    deco.titlebar = wlr_scene_rect_create(scene_tree, 0, cfg->title_h, cfg->color_title_inactive);
    wlr_scene_node_set_position(&deco.titlebar->node, 0, 0);

    deco.title_label = wlr_scene_buffer_create(scene_tree, nullptr);
    wlr_scene_node_set_enabled(&deco.title_label->node, cfg->show_title_text);

    deco.close_button = wlr_scene_rect_create(scene_tree, cfg->close_button_w, cfg->title_h,
                                              cfg->color_close_inactive);
    deco.maximize =
        wlr_scene_rect_create(scene_tree, cfg->maximize_button_w, cfg->title_h, cfg->color_button);
    deco.minimize =
        wlr_scene_rect_create(scene_tree, cfg->minimize_button_w, cfg->title_h, cfg->color_button);

    // create objects for corners and edges
    deco.border_top = wlr_scene_rect_create(scene_tree, 0, cfg->border_w, cfg->color_invisible);
    deco.border_left = wlr_scene_rect_create(scene_tree, cfg->border_w, 0, cfg->color_border);
    deco.border_right = wlr_scene_rect_create(scene_tree, cfg->border_w, 0, cfg->color_border);
    deco.border_bottom = wlr_scene_rect_create(scene_tree, 0, cfg->border_w, cfg->color_border);
    deco.corner_tl =
        wlr_scene_rect_create(scene_tree, cfg->corner_size, cfg->corner_size, cfg->color_invisible);
    deco.corner_tr =
        wlr_scene_rect_create(scene_tree, cfg->corner_size, cfg->corner_size, cfg->color_invisible);
    deco.corner_bl =
        wlr_scene_rect_create(scene_tree, cfg->corner_size, cfg->corner_size, cfg->color_invisible);
    deco.corner_br =
        wlr_scene_rect_create(scene_tree, cfg->corner_size, cfg->corner_size, cfg->color_invisible);

    deco_update();
}

// called when the decoration needs to be updated
void view::deco_update() const {
    if (decoration_mode != deco_mode::SERVER || !deco.titlebar) {
        return;
    }

    config* cfg = &srv->cfg;
    const int sw = toplevel->base->geometry.width;
    const int sh = toplevel->base->geometry.height;
    const int tw = sw + 2 * cfg->border_w;

    wlr_scene_rect_set_size(deco.titlebar, tw, cfg->title_h);

    wlr_scene_rect_set_size(deco.close_button, cfg->close_button_w, cfg->title_h - 4);
    const int close_x = tw - cfg->close_button_w - 4;
    wlr_scene_node_set_position(&deco.close_button->node, close_x, 0);

    wlr_scene_rect_set_size(deco.maximize, cfg->maximize_button_w, cfg->title_h - 4);
    const int maximize_x = close_x - 4 - cfg->maximize_button_w;
    wlr_scene_node_set_position(&deco.maximize->node, maximize_x, 0);

    wlr_scene_rect_set_size(deco.minimize, cfg->minimize_button_w, cfg->title_h - 4);
    const int minimize_x = maximize_x - 2 - cfg->minimize_button_w;
    wlr_scene_node_set_position(&deco.minimize->node, minimize_x, 0);

    if (deco.title_label) {
        // render the titlebar text
        wlr_scene_node_set_enabled(&deco.title_label->node, cfg->show_title_text);
        if (cfg->show_title_text) {
            const int label_w = minimize_x - 4;
            const char* title = toplevel->title ? toplevel->title : "";
            deco_render_title(deco.title_label, title, label_w, cfg->title_h,
                              cfg->color_title_text);
            wlr_scene_node_set_position(&deco.title_label->node, 0, 0);
        }
    }

    wlr_scene_rect_set_size(deco.border_top, tw, cfg->border_w);
    wlr_scene_node_set_position(&deco.border_top->node, 0, 0);

    wlr_scene_rect_set_size(deco.border_left, cfg->border_w, sh);
    wlr_scene_node_set_position(&deco.border_left->node, 0, cfg->title_h);

    wlr_scene_rect_set_size(deco.border_right, cfg->border_w, sh);
    wlr_scene_node_set_position(&deco.border_right->node, tw - cfg->border_w, cfg->title_h);

    wlr_scene_rect_set_size(deco.border_bottom, tw, cfg->border_w);
    wlr_scene_node_set_position(&deco.border_bottom->node, 0, cfg->title_h + sh);

    wlr_scene_node_set_position(&deco.corner_tl->node, 0, 0);
    wlr_scene_node_set_position(&deco.corner_tr->node, tw - cfg->corner_size, 0);

    const int corner_y = cfg->title_h + sh + cfg->border_w - cfg->corner_size;
    wlr_scene_node_set_position(&deco.corner_bl->node, 0, corner_y);
    wlr_scene_node_set_position(&deco.corner_br->node, tw - cfg->corner_size, corner_y);
}

// called when a view is destroyed
void view::deco_destroy() {
    if (decoration_mode != deco_mode::SERVER || !deco.titlebar) {
        return;
    }
    // free stuff
    deco.titlebar = nullptr;
    deco.title_label = nullptr;
    deco.close_button = nullptr;
    deco.maximize = nullptr;
    deco.minimize = nullptr;
    deco.border_top = nullptr;
    deco.border_left = nullptr;
    deco.border_right = nullptr;
    deco.border_bottom = nullptr;
    deco.corner_tl = nullptr;
    deco.corner_tr = nullptr;
    deco.corner_bl = nullptr;
    deco.corner_br = nullptr;
}

void view::deco_set_focus(const bool focused) const {
    if (decoration_mode != deco_mode::SERVER || !deco.titlebar) {
        return;
    }
    const config* cfg = &srv->cfg;
    wlr_scene_rect_set_color(deco.titlebar,
                             focused ? cfg->color_title_active : cfg->color_title_inactive);
    wlr_scene_rect_set_color(deco.close_button,
                             focused ? cfg->color_close_active : cfg->color_close_inactive);
    wlr_scene_rect_set_color(deco.maximize,
                             focused ? cfg->color_button : cfg->color_button_inactive);
    wlr_scene_rect_set_color(deco.minimize,
                             focused ? cfg->color_button : cfg->color_button_inactive);
}

const char* view::deco_cursor_name(const struct wlr_scene_node* node) const {
    if (decoration_mode != deco_mode::SERVER) {
        return nullptr;
    }

    // set cursor when in a resize area
    if (node == &deco.corner_tl->node) {
        return "nw-resize";
    }
    if (node == &deco.corner_tr->node) {
        return "ne-resize";
    }
    if (node == &deco.corner_bl->node) {
        return "sw-resize";
    }
    if (node == &deco.corner_br->node) {
        return "se-resize";
    }
    if (node == &deco.border_left->node) {
        return "w-resize";
    }
    if (node == &deco.border_right->node) {
        return "e-resize";
    }
    if (node == &deco.border_top->node) {
        return "n-resize";
    }
    if (node == &deco.border_bottom->node) {
        return "s-resize";
    }

    return nullptr;
}

view* steppewm::deco_at(const server* s, const double lx, const double ly,
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
        v->deco.title_label && hit == &v->deco.title_label->node) {
        *node = &v->deco.titlebar->node;
        return v;
    }

    return nullptr;
}

bool view::deco_handle_button(server* s, const struct wlr_scene_node* node, const uint32_t button) {
    if (decoration_mode != deco_mode::SERVER) {
        return false;
    }

    // handle close
    if (node == &deco.close_button->node) {
        if (button == BTN_LEFT) {
            wlr_xdg_toplevel_send_close(toplevel);
        }
        return true;
    }

    // handle maximize
    if (node == &deco.maximize->node) {
        if (button == BTN_LEFT) {
            toggle_maximize();
        }
        return true;
    }

    // handle minimize
    if (node == &deco.minimize->node) {
        if (button == BTN_LEFT) {
            minimize(true);
            view::focus_next(s, this);
        }
        return true;
    }
    // handle resizing
    if (node == &deco.titlebar->node) {
        cursor_begin_interactive(this, cursor_mode::MOVE, 0);
        return true;
    }
    if (node == &deco.corner_tl->node) {
        cursor_begin_interactive(this, cursor_mode::RESIZE, WLR_EDGE_TOP | WLR_EDGE_LEFT);
        return true;
    }
    if (node == &deco.corner_tr->node) {
        cursor_begin_interactive(this, cursor_mode::RESIZE, WLR_EDGE_TOP | WLR_EDGE_RIGHT);
        return true;
    }
    if (node == &deco.corner_bl->node) {
        cursor_begin_interactive(this, cursor_mode::RESIZE, WLR_EDGE_BOTTOM | WLR_EDGE_LEFT);
        return true;
    }
    if (node == &deco.corner_br->node) {
        cursor_begin_interactive(this, cursor_mode::RESIZE, WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
        return true;
    }
    if (node == &deco.border_left->node) {
        cursor_begin_interactive(this, cursor_mode::RESIZE, WLR_EDGE_LEFT);
        return true;
    }
    if (node == &deco.border_right->node) {
        cursor_begin_interactive(this, cursor_mode::RESIZE, WLR_EDGE_RIGHT);
        return true;
    }
    if (node == &deco.border_top->node) {
        cursor_begin_interactive(this, cursor_mode::RESIZE, WLR_EDGE_TOP);
        return true;
    }
    if (node == &deco.border_bottom->node) {
        cursor_begin_interactive(this, cursor_mode::RESIZE, WLR_EDGE_BOTTOM);
        return true;
    }

    return false;
}

// called when the client requests a decoration mode
static void deco_request_mode(view* v, struct wlr_xdg_toplevel_decoration_v1* decoration) {
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
static void deco_handle_destroy(view* v) {
    v->pending_deco = nullptr;
    v->decoration = nullptr;
    v->request_deco_mode.disconnect();
    v->destroy_deco.disconnect();
}

// called when a new xdg toplevel is created
void steppewm::deco_new(struct wl_listener* listener, void* data) {
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
