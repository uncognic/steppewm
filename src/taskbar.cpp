
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

#include "listener.h"
#include "paint.h"
#include "server.h"
#include "taskbar.h"
#include "view.h"

// one button in the task row, tracking the window it represents
struct steppewm_task_button {
    struct steppewm_taskbar* bar;
    struct steppewm_view* view;
    struct wlr_scene_buffer* label;
    steppe::Listener title_changed;
};

struct steppewm_taskbar {
    struct steppewm_server* server;
    struct wlr_output* wlr_output;
    struct wlr_scene_tree* tree;
    struct wlr_scene_rect* background;

    struct wlr_scene_buffer* clock;
    struct wl_event_source* clock_timer;

    struct wlr_scene_buffer* layout_ind; // keyboard layout indicator
    int layout_ind_w;                    // measured width of the layout indicator

    struct wlr_scene_buffer* ws_labels[STEPPEWM_NUM_WORKSPACES];
    int ws_button_w;

    std::vector<std::unique_ptr<steppewm_task_button>> buttons;

    int clock_w;  // measured width of the clock label
    int button_w; // dynamically adjusting button width

    int x, y, width, height;
};

// render button into cairo then draw it
static void render_button(struct wlr_scene_buffer* scene_buf, const char* text, int w, int h,
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

static void taskbar_layout(struct steppewm_taskbar* bar);

// render the current datetime into the clock buffer and pin it to the right edge
static void render_clock(struct steppewm_taskbar* bar) {
    if (bar->width <= 0 || bar->height <= 0 || !bar->clock) {
        return;
    }

    struct steppewm_config* cfg = &bar->server->config;
    int h = bar->height - 2 * cfg->taskbar_button_pad;
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
    int w = (int) (ext.width + 2 * pad);
    if (w <= 0) {
        return;
    }
    bar->clock_w = w;

    // draw onto a buffer sized to the text
    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    float* bg = cfg->color_taskbar_bg;
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    float* fg = cfg->color_task_text;
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
    cairo_move_to(cr, pad - ext.x_bearing, h / 2.0 - ext.y_bearing - ext.height / 2.0);
    cairo_show_text(cr, text);

    canvas.commit(bar->clock);

    wlr_scene_node_set_position(&bar->clock->node, bar->width - w - pad, pad);
}

// build the short layout code for the active keyboard group
static void taskbar_layout_code(struct steppewm_taskbar* bar, char* out, size_t len) {
    struct wlr_keyboard* kbd = wlr_seat_get_keyboard(bar->server->seat);
    uint32_t group = kbd ? kbd->modifiers.group : 0;

    // walk to the N-th comma-separated token in the configured layout string
    const char* tok = bar->server->config.xkb_layout;
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
static void render_layout_indicator(struct steppewm_taskbar* bar) {
    if (bar->width <= 0 || bar->height <= 0 || !bar->layout_ind) {
        return;
    }

    struct steppewm_config* cfg = &bar->server->config;
    int pad = cfg->taskbar_button_pad;
    int h = bar->height - 2 * pad;
    if (h <= 0) {
        return;
    }
    double font_size = h * 0.55;

    char text[32];
    taskbar_layout_code(bar, text, sizeof(text));

    cairo_text_extents_t ext = paint::text_extents(text, font_size);

    // size the badge to the text plus padding
    int w = (int) ext.width + h;
    if (w <= 0) {
        return;
    }
    bar->layout_ind_w = w;

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

    canvas.commit(bar->layout_ind);

    wlr_scene_node_set_position(&bar->layout_ind->node, bar->width - bar->clock_w - pad - w - pad,
                                pad);
}

// redraw the clock and rearm the timer to fire on the next minute boundary
static int clock_tick(void* data) {
    struct steppewm_taskbar* bar = static_cast<struct steppewm_taskbar*>(data);
    // full layout so buttons rerender if the clock's width changed
    taskbar_layout(bar);

    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    int ms = (60 - tm.tm_sec) * 1000;
    if (ms <= 0) {
        ms = 60000;
    }
    wl_event_source_timer_update(bar->clock_timer, ms);
    return 0;
}

// find current keyboard-focused steppewm_view and return it
static struct steppewm_view* taskbar_focused_view(struct steppewm_taskbar* bar) {
    struct wlr_surface* surf = bar->server->seat->keyboard_state.focused_surface;
    if (!surf) {
        return nullptr;
    }
    struct wlr_xdg_surface* xdg = wlr_xdg_surface_try_from_wlr_surface(surf);
    if (!xdg || xdg->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return nullptr;
    }
    return static_cast<struct steppewm_view*>(xdg->toplevel->base->data);
}

// redraw taskbar
static void taskbar_layout(struct steppewm_taskbar* bar) {
    // if the size is negative
    if (bar->width <= 0 || bar->height <= 0) {
        return;
    }

    // set sizes and background color
    wlr_scene_rect_set_size(bar->background, bar->width, bar->height);
    wlr_scene_rect_set_color(bar->background, bar->server->config.color_taskbar_bg);

    render_clock(bar);
    render_layout_indicator(bar);

    struct steppewm_config* cfg = &bar->server->config;
    int pad = cfg->taskbar_button_pad;
    int button_h = bar->height - 2 * pad;
    if (button_h < 1) {
        button_h = 1;
    }
    int current = bar->server->current_workspace;

    // workspace indicator buttons
    // width is the same as height since it is square
    int ws_button_w = button_h;
    bar->ws_button_w = ws_button_w;
    int cursor_x = pad;

    // draw each button
    for (int i = 0; i < STEPPEWM_NUM_WORKSPACES; i++) {
        wlr_scene_node_set_position(&bar->ws_labels[i]->node, cursor_x, pad);
        char num[4];
        snprintf(num, sizeof(num), "%d", i + 1);
        float* bg = (i == current) ? cfg->color_task_active : cfg->color_task_normal;
        render_button(bar->ws_labels[i], num, ws_button_w, button_h, bg, cfg->color_task_text);
        cursor_x += ws_button_w + pad;
    }

    // first task button starts just past the indicators
    int task_row_left = cursor_x;

    // count windows that live on the current workspace
    int visible_count = 0;
    for (auto& btn : bar->buttons) {
        if (btn->view->workspace == current) {
            visible_count++;
        }
    }

    // if there's no windows on the current workspace, hide all of them
    if (visible_count == 0) {
        for (auto& btn : bar->buttons) {
            wlr_scene_node_set_enabled(&btn->label->node, false);
        }
        return;
    }

    struct steppewm_view* focused_view = taskbar_focused_view(bar);

    // task row ends before the layout indicator and the clock on the right
    int right_limit = bar->width - bar->clock_w - pad - bar->layout_ind_w - pad;
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
    bar->button_w = button_w;

    // draw each window button
    int slot = 0;
    for (auto& btn : bar->buttons) {
        // skip and hide windows on other workspaces
        if (btn->view->workspace != current) {
            wlr_scene_node_set_enabled(&btn->label->node, false);
            continue;
        }
        wlr_scene_node_set_enabled(&btn->label->node, true);

        // set top left corner of the button
        int button_x = task_row_left + slot * (button_w + pad);
        wlr_scene_node_set_position(&btn->label->node, button_x, pad);

        float* bg;
        if (btn->view == focused_view) {
            bg = cfg->color_task_active;
        } else if (btn->view->minimized) {
            bg = cfg->color_task_minimized;
        } else {
            bg = cfg->color_task_normal;
        }

        const char* title = btn->view->toplevel->title ? btn->view->toplevel->title : "";
        render_button(btn->label, title, button_w, button_h, bg, cfg->color_task_text);
        slot++;
    }
}

// create taskbar, scene tree, and bg
struct steppewm_taskbar* taskbar_create(struct steppewm_server* server,
                                        struct wlr_output* wlr_output) {
    auto* bar = new steppewm_taskbar();
    bar->server = server;
    bar->wlr_output = wlr_output;
    bar->height = server->config.taskbar_h;
    bar->tree = wlr_scene_tree_create(&server->scene->tree);
    bar->background =
        wlr_scene_rect_create(bar->tree, 0, bar->height, server->config.color_taskbar_bg);

    for (int i = 0; i < STEPPEWM_NUM_WORKSPACES; i++) {
        bar->ws_labels[i] = wlr_scene_buffer_create(bar->tree, nullptr);
    }

    // keyboard layout indicator
    bar->layout_ind = wlr_scene_buffer_create(bar->tree, nullptr);

    // clock label and a timer that ticks it over each minute
    bar->clock = wlr_scene_buffer_create(bar->tree, nullptr);
    struct wl_event_loop* loop = wl_display_get_event_loop(server->display);
    bar->clock_timer = wl_event_loop_add_timer(loop, clock_tick, bar);
    wl_event_source_timer_update(bar->clock_timer, 1);

    return bar;
}

// destroy and free bar
void taskbar_destroy(struct steppewm_taskbar* bar) {
    if (bar->clock_timer) {
        wl_event_source_remove(bar->clock_timer);
    }
    delete bar;
}

// create new taskbar button for new window
void taskbar_view_added(struct steppewm_taskbar* bar, struct steppewm_view* view) {
    auto btn = std::make_unique<steppewm_task_button>();
    btn->bar = bar;
    btn->view = view;
    btn->label = wlr_scene_buffer_create(bar->tree, nullptr);

    // redraw the taskbar whenever this window's title changes
    btn->title_changed.connect(&view->toplevel->events.set_title,
                               [bar](void*) { taskbar_layout(bar); });

    bar->buttons.push_back(std::move(btn));
    taskbar_layout(bar);
}

// remove window's taskbar button and redraw the taskbar
void taskbar_view_removed(struct steppewm_taskbar* bar, struct steppewm_view* view) {
    for (auto it = bar->buttons.begin(); it != bar->buttons.end(); ++it) {
        if ((*it)->view != view) {
            continue;
        }
        wlr_scene_node_destroy(&(*it)->label->node);
        bar->buttons.erase(it);
        taskbar_layout(bar);
        return;
    }
}

// refresh taskbar
void taskbar_refresh(struct steppewm_taskbar* bar) {
    taskbar_layout(bar);
}

// update size and position
void taskbar_update_geometry(struct steppewm_taskbar* bar) {
    struct wlr_box box;
    wlr_output_layout_get_box(bar->server->output_layout, bar->wlr_output, &box);
    if (box.width <= 0 || box.height <= 0) {
        return;
    }

    // set properties
    bar->x = box.x;
    bar->width = box.width;
    bar->height = bar->server->config.taskbar_h;
    bar->y = box.y + box.height - bar->height;

    wlr_scene_node_set_position(&bar->tree->node, bar->x, bar->y);
    taskbar_layout(bar);
}

// raise the taskbar's scene tree above the windows below it
void taskbar_raise(struct steppewm_taskbar* bar) {
    wlr_scene_node_raise_to_top(&bar->tree->node);
}

// return corresponding steppewm_view depending on which taskbar button is at the xy position
struct steppewm_view* taskbar_view_at(struct steppewm_taskbar* bar, double x, double y) {
    // safety
    if (bar->buttons.empty() || bar->width <= 0) {
        return nullptr;
    }
    if (y < bar->y || y >= bar->y + bar->height) {
        return nullptr;
    }
    if (x < bar->x || x >= bar->x + bar->width) {
        return nullptr;
    }

    // find x position relative to the taskbar's left edge
    int local_x = (int) (x - bar->x);

    struct steppewm_config* cfg = &bar->server->config;
    int pad = cfg->taskbar_button_pad;
    int ws_button_w = bar->ws_button_w > 0 ? bar->ws_button_w : (bar->height - 2 * pad);
    int button_w = bar->button_w > 0 ? bar->button_w : cfg->taskbar_button_w;

    // task buttons begin just past the workspace indicators
    int task_row_left = pad + STEPPEWM_NUM_WORKSPACES * (ws_button_w + pad);
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
    int current = bar->server->current_workspace;
    int count = 0;
    for (auto& btn : bar->buttons) {
        if (btn->view->workspace != current) {
            continue;
        }
        if (count == slot) {
            return btn->view;
        }
        count++;
    }
    return nullptr;
}

int taskbar_workspace_at(struct steppewm_taskbar* bar, double x, double y) {
    if (bar->width <= 0) {
        return -1;
    }
    if (y < bar->y || y >= bar->y + bar->height) {
        return -1;
    }
    if (x < bar->x || x >= bar->x + bar->width) {
        return -1;
    }

    int local_x = (int) (x - bar->x);
    struct steppewm_config* cfg = &bar->server->config;
    int pad = cfg->taskbar_button_pad;
    int ws_button_w = bar->ws_button_w > 0 ? bar->ws_button_w : (bar->height - 2 * pad);
    if (ws_button_w < 1) {
        ws_button_w = 1;
    }

    int idx = (local_x - pad) / (ws_button_w + pad);
    if (idx < 0 || idx >= STEPPEWM_NUM_WORKSPACES) {
        return -1;
    }
    // reject clicks in the gap between indicators
    if ((local_x - pad) - idx * (ws_button_w + pad) >= ws_button_w) {
        return -1;
    }
    return idx;
}
