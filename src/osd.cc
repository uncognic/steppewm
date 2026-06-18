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

#include "wlr.h" // must be first

#include <cairo/cairo.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>

#include "osd.h"
#include "paint.h"
#include "server.h"

using namespace steppewm;

osd::osd(server* s) : srv_(s) {
    tree_ = wlr_scene_tree_create(&s->scene->tree);
    wlr_scene_node_set_enabled(&tree_->node, false);
    buf_ = wlr_scene_buffer_create(tree_, nullptr);

    wl_event_loop* loop = wl_display_get_event_loop(s->display);
    timer_ = wl_event_loop_add_timer(loop, on_timeout, this);
}

osd::~osd() {
    if (timer_) {
        wl_event_source_remove(timer_);
    }
}

void osd::show(const char* text) const {
    wlr_output* wlr_out =
        wlr_output_layout_output_at(srv_->output_layout, srv_->cursor->x, srv_->cursor->y);
    if (!wlr_out) {
        return;
    }

    wlr_box box;
    wlr_output_layout_get_box(srv_->output_layout, wlr_out, &box);
    if (box.width <= 0 || box.height <= 0) {
        return;
    }

    double font_size = box.height * 0.018;
    if (font_size < 14) {
        font_size = 14;
    }
    if (font_size > 32) {
        font_size = 32;
    }

    const cairo_text_extents_t ext = paint::text_extents(text, font_size);

    const int pad_x = static_cast<int>(font_size * 1.0);
    const int pad_y = static_cast<int>(font_size * 0.6);
    const int w = static_cast<int>(ext.width) + 2 * pad_x;
    const int h = static_cast<int>(ext.height) + 2 * pad_y;
    if (w <= 0 || h <= 0) {
        return;
    }

    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 0.95);
    cairo_paint(cr);

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    const double tx = (w - ext.width) / 2.0 - ext.x_bearing;
    const double ty = (h - ext.height) / 2.0 - ext.y_bearing;
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, text);

    canvas.commit(buf_);

    const int ox = box.x + (box.width - w) / 2;
    const int oy = box.y + box.height * 3 / 4;

    wlr_scene_node_set_position(&tree_->node, ox, oy);
    wlr_scene_node_set_enabled(&tree_->node, true);
    wlr_scene_node_raise_to_top(&tree_->node);

    wl_event_source_timer_update(timer_, 1500);
}

int osd::on_timeout(void* data) {
    const auto* o = static_cast<osd*>(data);
    wlr_scene_node_set_enabled(&o->tree_->node, false);
    return 0;
}
