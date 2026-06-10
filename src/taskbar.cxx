
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

#include "wlr.hxx" // must be first

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <memory>
#include <vector>

#include <cairo/cairo.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "config.hxx"
#include "listener.hxx"
#include "paint.hxx"
#include "server.hxx"
#include "taskbar.hxx"
#include "view.hxx"

// one button in the task row, tracking the window it represents
namespace steppewm {
struct task_button {
    view* v;
    struct wlr_scene_buffer* label;
    Listener title_changed;
};
} // namespace steppewm

using namespace steppewm;

// render button into cairo then draw it
void taskbar::render_button(struct wlr_scene_buffer* scene_buf, const char* text, int w, int h,
                            float bg[4], float fg[4]) {
    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    if (text && text[0]) {
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, h * 0.55);
        cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, text, &ext);
        double tx = (w - ext.width) / 2.0 - ext.x_bearing;
        if (tx < 4.0) {
            tx = 4.0;
        }
        double ty = h / 2.0 - ext.y_bearing - ext.height / 2.0;
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, text);
    }

    canvas.commit(scene_buf);
}

// render the current datetime into the clock buffer and pin it to the right edge
void taskbar::render_clock() {
    if (width_ <= 0 || height_ <= 0 || !clock_) {
        return;
    }

    config* cfg = &srv_->cfg;
    int h = height_ - 2 * cfg->taskbar_button_pad;
    if (h <= 0) {
        return;
    }
    double font_size = h * 0.55;

    // build the time string
    char text[64];
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(text, sizeof(text), "%-d %b %Y %I:%M %p", &tm);

    // measure the text so we can size the buffer to fit
    cairo_text_extents_t ext = paint::text_extents(text, font_size);

    int pad = cfg->taskbar_button_pad;
    int w = (int) ext.width + h;
    if (w <= 0) {
        return;
    }
    clock_w_ = w;

    // draw onto a buffer sized to the text
    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    float* bg = cfg->color_task_normal;
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    float* fg = cfg->color_task_text;
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
    double tx = (w - ext.width) / 2.0 - ext.x_bearing;
    double ty = h / 2.0 - ext.y_bearing - ext.height / 2.0;
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, text);

    canvas.commit(clock_);

    wlr_scene_node_set_position(&clock_->node, width_ - w - pad, pad);
}

// build the short layout code for the active keyboard group
void taskbar::layout_code(char* out, size_t len) {
    struct wlr_keyboard* kbd = wlr_seat_get_keyboard(srv_->seat);
    uint32_t group = kbd ? kbd->modifiers.group : 0;

    // walk to the N-th comma-separated token in the configured layout string
    const char* tok = srv_->cfg.xkb_layout;
    for (uint32_t i = 0; i < group && tok && *tok; i++) {
        const char* comma = strchr(tok, ',');
        tok = comma ? comma + 1 : nullptr;
    }

    size_t n = 0;
    if (tok) {
        while (tok[n] && tok[n] != ',' && n + 1 < len) {
            out[n] = (char) toupper((unsigned char) tok[n]);
            n++;
        }
    }
    // no layout configured
    if (n == 0) {
        snprintf(out, len, "US");
        return;
    }
    out[n] = '\0';
}

// render the keyboard layout indicator and pin it to the right, just left of the clock
void taskbar::render_layout_indicator() {
    if (width_ <= 0 || height_ <= 0 || !layout_ind_) {
        return;
    }

    config* cfg = &srv_->cfg;
    int pad = cfg->taskbar_button_pad;
    int h = height_ - 2 * pad;
    if (h <= 0) {
        return;
    }
    double font_size = h * 0.55;

    char text[32];
    layout_code(text, sizeof(text));

    cairo_text_extents_t ext = paint::text_extents(text, font_size);

    // size the badge to the text plus padding
    int w = (int) ext.width + h;
    if (w <= 0) {
        return;
    }
    layout_ind_w_ = w;

    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    float* bg = cfg->color_task_normal;
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    float* fg = cfg->color_task_text;
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
    double tx = (w - ext.width) / 2.0 - ext.x_bearing;
    double ty = h / 2.0 - ext.y_bearing - ext.height / 2.0;
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, text);

    canvas.commit(layout_ind_);

    wlr_scene_node_set_position(&layout_ind_->node, width_ - clock_w_ - pad - w - pad, pad);
}

// redraw the clock and rearm the timer to fire on the next minute boundary
int taskbar::clock_tick(void* data) {
    taskbar* bar = static_cast<taskbar*>(data);
    // full layout so buttons rerender if the clock's width changed
    bar->layout();

    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    int ms = (60 - tm.tm_sec) * 1000;
    if (ms <= 0) {
        ms = 60000;
    }
    wl_event_source_timer_update(bar->clock_timer_, ms);
    return 0;
}

int taskbar::urgent_tick(void* data) {
    auto* bar = static_cast<taskbar*>(data);
    bar->urgent_flash_on_ = !bar->urgent_flash_on_;
    bar->layout();
    return 0;
}

// find current keyboard-focused steppewm_view and return it
view* taskbar::focused_view() const {
    struct wlr_surface* surf = srv_->seat->keyboard_state.focused_surface;
    if (!surf) {
        return nullptr;
    }
    struct wlr_xdg_surface * xdg = wlr_xdg_surface_try_from_wlr_surface(surf);
    if (!xdg || xdg->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return nullptr;
    }
    return static_cast<view*>(xdg->toplevel->base->data);
}

// redraw taskbar
void taskbar::layout() {
    // if the size is negative
    if (width_ <= 0 || height_ <= 0) {
        return;
    }

    // set sizes and background color
    wlr_scene_rect_set_size(background_, width_, height_);
    wlr_scene_rect_set_color(background_, srv_->cfg.color_taskbar_bg);

    render_clock();
    render_layout_indicator();

    config* cfg = &srv_->cfg;
    int pad = cfg->taskbar_button_pad;
    int button_h = height_ - 2 * pad;
    if (button_h < 1) {
        button_h = 1;
    }
    int current = srv_->current_workspace;

    // workspace indicator buttons
    // width is the same as height since it is square
    int ws_button_w = button_h;
    ws_button_w_ = ws_button_w;
    int cursor_x = pad;

    // draw each button
    for (int i = 0; i < num_workspaces; i++) {
        wlr_scene_node_set_position(&ws_labels_[i]->node, cursor_x, pad);
        char num[4];
        snprintf(num, sizeof(num), "%d", i + 1);
        float *bg = (i == current) ? cfg->color_task_active : cfg->color_task_normal;
        render_button(ws_labels_[i], num, ws_button_w, button_h, bg, cfg->color_task_text);
        cursor_x += ws_button_w + pad;
    }

    // first task button starts just past the indicators
    int task_row_left = cursor_x;

    // count windows that live on the current workspace
    int visible_count = 0;
    for (auto &btn : buttons_) {
        if (btn->v->workspace == current) {
            visible_count++;
        }
    }

    // if there's no windows on the current workspace, hide all of them
    if (visible_count == 0) {
        for (auto &btn : buttons_) {
            wlr_scene_node_set_enabled(&btn->label->node, false);
        }
        return;
    }

    view *fv = focused_view();

    // task row ends before the layout indicator and the clock on the right
    int right_limit = width_ - clock_w_ - pad - layout_ind_w_ - pad;
    int task_row_width = right_limit - pad - task_row_left;

    // divide by number of visible buttons to get width
    int button_w = (task_row_width - visible_count * pad) / visible_count;

    // don't exceed configured max
    if (button_w > cfg->taskbar_button_w) {
        button_w = cfg->taskbar_button_w;
    }

    // never zero
    if (button_w < 1) {
        button_w = 1;
    }
    button_w_ = button_w;

    // draw each window button
    int slot = 0;
    bool has_visible_urgent = false;
    for (auto &btn : buttons_) {
        // skip and hide windows on other workspaces
        if (btn->v->workspace != current) {
            wlr_scene_node_set_enabled(&btn->label->node, false);
            continue;
        }
        wlr_scene_node_set_enabled(&btn->label->node, true);

        // set top left corner of the button
        int button_x = task_row_left + slot * (button_w + pad);
        wlr_scene_node_set_position(&btn->label->node, button_x, pad);

        float *bg;
        if (btn->v == fv) {
            bg = cfg->color_task_active;
        } else if (btn->v->urgent) {
            has_visible_urgent = true;
            bg = urgent_flash_on_
                     ? cfg->color_task_urgent
                     : (btn->v->minimized ? cfg->color_task_minimized : cfg->color_task_normal);
        } else if (btn->v->minimized) {
            bg = cfg->color_task_minimized;
        } else {
            bg = cfg->color_task_normal;
        }

        const char *title = btn->v->toplevel->title ? btn->v->toplevel->title : "";
        render_button(btn->label, title, button_w, button_h, bg, cfg->color_task_text);
        slot++;
    }

    if (has_visible_urgent) {
        wl_event_source_timer_update(urgent_timer_, 500);
    } else {
        urgent_flash_on_ = true;
    }
}

// create taskbar, scene tree, and bg
taskbar::taskbar(server *s, struct wlr_output* wlr_output) {
    srv_ = s;
    wlr_output_ = wlr_output;
    height_ = s->cfg.taskbar_h;
    tree_ = wlr_scene_tree_create(&s->scene->tree);
    background_ = wlr_scene_rect_create(tree_, 0, height_, s->cfg.color_taskbar_bg);

    for (int i = 0; i < num_workspaces; i++) {
        ws_labels_[i] = wlr_scene_buffer_create(tree_, nullptr);
    }

    // keyboard layout indicator
    layout_ind_ = wlr_scene_buffer_create(tree_, nullptr);

    // clock label and a timer that ticks it over each minute
    clock_ = wlr_scene_buffer_create(tree_, nullptr);
    struct wl_event_loop* loop = wl_display_get_event_loop(s->display);
    clock_timer_ = wl_event_loop_add_timer(loop, clock_tick, this);
    wl_event_source_timer_update(clock_timer_, 1);
    urgent_timer_ = wl_event_loop_add_timer(loop, urgent_tick, this);
}

// destroy bar
taskbar::~taskbar() {
    if (clock_timer_) {
        wl_event_source_remove(clock_timer_);
    }
    if (urgent_timer_) {
        wl_event_source_remove(urgent_timer_);
    }
}

// create new taskbar button for new window
void taskbar::view_added(view* v) {
    auto btn = std::make_unique<task_button>();
    btn->v = v;
    btn->label = wlr_scene_buffer_create(tree_, nullptr);

    // redraw the taskbar whenever this window's title changes
    btn->title_changed.connect(&v->toplevel->events.set_title, [this](void*) { layout(); });

    buttons_.push_back(std::move(btn));
    layout();
}

// remove window's taskbar button and redraw the taskbar
void taskbar::view_removed(view *v) {
    for (auto it = buttons_.begin(); it != buttons_.end(); ++it) {
        if ((*it)->v != v) {
            continue;
        }
        wlr_scene_node_destroy(&(*it)->label->node);
        buttons_.erase(it);
        layout();
        return;
    }
}

// refresh taskbar
void taskbar::refresh() {
    layout();
}

// update size and position
void taskbar::update_geometry() {
    struct wlr_box box;
    wlr_output_layout_get_box(srv_->output_layout, wlr_output_, &box);
    if (box.width <= 0 || box.height <= 0) {
        return;
    }

    // set properties
    x_ = box.x;
    width_ = box.width;
    height_ = srv_->cfg.taskbar_h;
    y_ = box.y + box.height - height_;

    wlr_scene_node_set_position(&tree_->node, x_, y_);
    layout();
}

// raise the taskbar's scene tree above the windows below it
void taskbar::raise() {
    wlr_scene_node_raise_to_top(&tree_->node);
}

// return corresponding steppewm_view depending on which taskbar button is at the xy position
view *taskbar::view_at(double x, double y) {
    // safety
    if (buttons_.empty() || width_ <= 0) {
        return nullptr;
    }
    if (y < y_ || y >= y_ + height_) {
        return nullptr;
    }
    if (x < x_ || x >= x_ + width_) {
        return nullptr;
    }

    // find x position relative to the taskbar's left edge
    int local_x = (int)(x - x_);

    config *cfg = &srv_->cfg;
    int pad = cfg->taskbar_button_pad;
    int ws_button_w = ws_button_w_ > 0 ? ws_button_w_ : (height_ - 2 * pad);
    int button_w = button_w_ > 0 ? button_w_ : cfg->taskbar_button_w;

    // task buttons begin just past the workspace indicators
    int task_row_left = pad + num_workspaces * (ws_button_w + pad);
    if (local_x < task_row_left) {
        return nullptr;
    }

    // find which visible slot we are on
    int slot = (local_x - task_row_left) / (button_w + pad);
    if (slot < 0) {
        return nullptr;
    }

    // reject clicks in the gap or past the button's right edge
    if ((local_x - task_row_left) - slot * (button_w + pad) >= button_w) {
        return nullptr;
    }

    // map the slot to the slot-th window on the current workspace
    int current = srv_->current_workspace;
    int count = 0;
    for (auto& btn : buttons_) {
        if (btn->v->workspace != current) {
            continue;
        }
        if (count == slot) {
            return btn->v;
        }
        count++;
    }
    return nullptr;
}

int taskbar::workspace_at(double x, double y) {
    if (width_ <= 0) {
        return -1;
    }
    if (y < y_ || y >= y_ + height_) {
        return -1;
    }
    if (x < x_ || x >= x_ + width_) {
        return -1;
    }

    int local_x = (int) (x - x_);
    config *cfg = &srv_->cfg;
    int pad = cfg->taskbar_button_pad;
    int ws_button_w = ws_button_w_ > 0 ? ws_button_w_ : (height_ - 2 * pad);
    if (ws_button_w < 1) {
        ws_button_w = 1;
    }

    int idx = (local_x - pad) / (ws_button_w + pad);
    if (idx < 0 || idx >= num_workspaces) {
        return -1;
    }
    // reject clicks in the gap between indicators
    if ((local_x - pad) - idx * (ws_button_w + pad) >= ws_button_w) {
        return -1;
    }
    return idx;
}
