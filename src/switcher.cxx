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

#include <vector>

#include <cairo/cairo.h>

#include "output.hxx"
#include "paint.hxx"
#include "server.hxx"
#include "switcher.hxx"
#include "view.hxx"

using namespace steppewm;

#define SWITCHER_W 400
#define SWITCHER_ROW_H 30
#define SWITCHER_PAD 3

switcher::switcher(server* s, std::vector<view*> views, uint32_t mods)
    : server_(s), views_(std::move(views)), mods_(mods) {
    tree_ = wlr_scene_tree_create(&s->scene->tree);
    panel_ = wlr_scene_buffer_create(tree_, nullptr);
    wlr_scene_node_raise_to_top(&tree_->node);
    s->sw = this;
}

switcher::~switcher() {
    wlr_scene_node_destroy(&tree_->node);
    server_->sw = nullptr;
}

// pick the output under the cursor, falling back to the first output
struct wlr_output* switcher::pick_output() const {
    struct wlr_output* wlr_out =
        wlr_output_layout_output_at(server_->output_layout, server_->cursor->x, server_->cursor->y);
    if (!wlr_out) {
        output* o;
        wl_list_for_each(o, &server_->outputs, link) {
            wlr_out = o->wlr_output;
            break;
        }
    }
    return wlr_out;
}

// render the switcher and center it on the output
void switcher::render() const {
    struct wlr_output* wlr_out = pick_output();
    if (!wlr_out) {
        return;
    }

    struct wlr_box ob;
    wlr_output_layout_get_box(server_->output_layout, wlr_out, &ob);

    const int n = static_cast<int>(views_.size());

    // make sure it's not wider than the screen
    int w = SWITCHER_W;
    if (w > ob.width - 2 * SWITCHER_PAD) {
        w = ob.width - 2 * SWITCHER_PAD;
    }

    // make sure height is not zero
    const int h = n * SWITCHER_ROW_H + (n + 1) * SWITCHER_PAD;
    if (w <= 0 || h <= 0) {
        return;
    }

    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    config* cfg = &server_->cfg;
    float* bg = cfg->color_taskbar_bg;
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    double font_size = SWITCHER_ROW_H * 0.55;

    // draw each window's row
    for (int i = 0; i < n; i++) {
        // row y height
        int row_y = SWITCHER_PAD + i * (SWITCHER_ROW_H + SWITCHER_PAD);

        // highlight the selected row like an active taskbar button
        float* row_bg = (size_t) i == selected_ ? cfg->color_task_active : cfg->color_task_normal;
        cairo_set_source_rgba(cr, row_bg[0], row_bg[1], row_bg[2], row_bg[3]);
        cairo_rectangle(cr, SWITCHER_PAD, row_y, w - 2 * SWITCHER_PAD, SWITCHER_ROW_H);
        cairo_fill(cr);

        const char* title = views_[i]->toplevel->title ? views_[i]->toplevel->title : "";
        if (!title[0]) {
            continue;
        }

        // clip so long titles stay inside their row
        cairo_save(cr);
        cairo_rectangle(cr, SWITCHER_PAD, row_y, w - 2 * SWITCHER_PAD, SWITCHER_ROW_H);
        cairo_clip(cr);

        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, font_size);
        float* fg = cfg->color_task_text;
        cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, title, &ext);

        // center title horizontally in the switcher
        double tx = (w - ext.width) / 2.0 - ext.x_bearing;

        // prevent it from touching the edge
        if (tx < SWITCHER_PAD + 4.0) {
            tx = SWITCHER_PAD + 4.0;
        }

        // center title vertically
        double ty = row_y + SWITCHER_ROW_H / 2.0 - ext.y_bearing - ext.height / 2.0;
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, title);
        cairo_restore(cr);
    }

    canvas.commit(panel_);

    wlr_scene_node_set_position(&tree_->node, ob.x + (ob.width - w) / 2,
                                ob.y + (ob.height - h) / 2);
}

// move the highlight one step through the snapshot
void switcher::advance(bool backwards) {
    size_t count = views_.size();
    selected_ = backwards ? (selected_ + count - 1) % count : (selected_ + 1) % count;
    render();
}

// open the overlay on the first press, then step the highlight on each repeat
void switcher::cycle(server* s, uint32_t mods, bool backwards) {
    switcher* sw = s->sw;

    // if this is the first press
    if (!sw) {
        // get the windows on this workspace, minimized ones included
        std::vector<view*> views;
        view* v;
        wl_list_for_each(v, &s->views, link) {
            if (v->mapped && v->workspace == s->current_workspace) {
                views.push_back(v);
            }
        }
        if (views.empty()) {
            return;
        }
        sw = new switcher(s, std::move(views), mods);
    }

    // otherwise advance
    sw->advance(backwards);
}

// commits once the cycle's modifiers are released
void switcher::handle_modifiers(server* s, uint32_t mods) {
    switcher* sw = s->sw;
    if (!sw || (mods & sw->mods_) != 0) {
        return;
    }

    view* v = sw->views_[sw->selected_];

    delete sw;

    v->focus(v->toplevel->base->surface);
}

void switcher::cancel(const server* s) {
    delete s->sw;
}

// called when a view is removed mid-cycle
void switcher::view_removed(const server* s, const view* v) {
    switcher* sw = s->sw;
    if (!sw) {
        return;
    }

    for (size_t i = 0; i < sw->views_.size(); i++) {
        // if it's not the one we want to remove
        if (sw->views_[i] != v) {
            continue;
        }

        sw->views_.erase(sw->views_.begin() + static_cast<long>(i));

        // if that was the last window
        if (sw->views_.empty()) {
            delete sw;
            return;
        }

        // keep the highlight on the same window where possible
        if (sw->selected_ > i) {
            sw->selected_--;
        } else if (sw->selected_ >= sw->views_.size()) {
            sw->selected_ = sw->views_.size() - 1;
        }
        sw->render();
        return;
    }
}
