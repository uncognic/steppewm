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
#include <time.h>

#include <cairo/cairo.h>
#include <drm_fourcc.h>

#include <wayland-server-core.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "server.h"
#include "taskbar.h"
#include "view.h"

// cpu backed wlr_buffer for cairo
struct cpu_buf {
    struct wlr_buffer base;
    uint8_t *pixels;
    size_t stride;
};

// free mem and struct
static void cpu_buf_destroy(struct wlr_buffer *wlr_buf) {
    struct cpu_buf *buf = wl_container_of(wlr_buf, buf, base);
    free(buf->pixels);
    free(buf);
}

// put cpu_buf data into a wlr_buf
static bool cpu_buf_begin_data_ptr_access(struct wlr_buffer *wlr_buf, uint32_t flags, void **data,
                                          uint32_t *format, size_t *stride) {
    (void) flags;
    struct cpu_buf *buf = wl_container_of(wlr_buf, buf, base);
    *data = buf->pixels;
    *format = DRM_FORMAT_ARGB8888;
    *stride = buf->stride;
    return true;
}

// no cleanup needed
static void cpu_buf_end_data_ptr_access(struct wlr_buffer *wlr_buf) {
    (void) wlr_buf;
}

static const struct wlr_buffer_impl cpu_buf_impl = {
    .destroy = cpu_buf_destroy,
    .begin_data_ptr_access = cpu_buf_begin_data_ptr_access,
    .end_data_ptr_access = cpu_buf_end_data_ptr_access,
};

// allocate and init wlr_buffer
static struct cpu_buf *cpu_buf_create(int w, int h) {
    struct cpu_buf *buf = calloc(1, sizeof(*buf));
    buf->stride = (size_t) w * 4;
    buf->pixels = calloc(h, buf->stride);
    wlr_buffer_init(&buf->base, &cpu_buf_impl, w, h);
    return buf;
}

// render button into cairo then draw it
static void render_button(struct wlr_scene_buffer *scene_buf, const char *text, int w, int h,
                          float bg[4], float fg[4]) {
    if (w <= 0 || h <= 0) {
        return;
    }

    // create cairo surface from data
    struct cpu_buf *buf = cpu_buf_create(w, h);
    cairo_surface_t *surf = cairo_image_surface_create_for_data(buf->pixels, CAIRO_FORMAT_ARGB32, w,
                                                                h, (int) buf->stride);
    cairo_t *cr = cairo_create(surf);

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

    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    wlr_scene_buffer_set_buffer(scene_buf, &buf->base);
    wlr_buffer_drop(&buf->base);
}

static void taskbar_layout(struct steppewm_taskbar *bar);

// render the current datetime into the clock buffer and pin it to the right edge
static void render_clock(struct steppewm_taskbar *bar) {
    if (bar->width <= 0 || bar->height <= 0 || !bar->clock) {
        return;
    }

    struct steppewm_config *cfg = &bar->server->config;
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

    // measure the text on a throwaway surface so we can size the buffer to fit
    cairo_surface_t *measure = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *mcr = cairo_create(measure);
    cairo_select_font_face(mcr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(mcr, font_size);
    cairo_text_extents_t ext;
    cairo_text_extents(mcr, text, &ext);
    cairo_destroy(mcr);
    cairo_surface_destroy(measure);

    int pad = cfg->taskbar_button_pad;
    int w = (int) (ext.width + 2 * pad);
    if (w <= 0) {
        return;
    }
    bar->clock_w = w;

    // draw onto a buffer sized to the text
    struct cpu_buf *buf = cpu_buf_create(w, h);
    cairo_surface_t *surf = cairo_image_surface_create_for_data(buf->pixels, CAIRO_FORMAT_ARGB32, w,
                                                                h, (int) buf->stride);
    cairo_t *cr = cairo_create(surf);

    float *bg = cfg->color_taskbar_bg;
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    float *fg = cfg->color_task_text;
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
    cairo_move_to(cr, pad - ext.x_bearing, h / 2.0 - ext.y_bearing - ext.height / 2.0);
    cairo_show_text(cr, text);

    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    wlr_scene_buffer_set_buffer(bar->clock, &buf->base);
    wlr_buffer_drop(&buf->base);

    wlr_scene_node_set_position(&bar->clock->node, bar->width - w - pad, pad);
}

// redraw the clock and rearm the timer to fire on the next minute boundary
static int clock_tick(void *data) {
    struct steppewm_taskbar *bar = data;
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
static struct steppewm_view *taskbar_focused_view(struct steppewm_taskbar *bar) {
    struct wlr_surface *surf = bar->server->seat->keyboard_state.focused_surface;
    if (!surf) {
        return nullptr;
    }
    struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(surf);
    if (!xdg || xdg->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return nullptr;
    }
    return xdg->toplevel->base->data;
}

// redraw taskbar
static void taskbar_layout(struct steppewm_taskbar *bar) {
    // if the size is negative
    if (bar->width <= 0 || bar->height <= 0) {
        return;
    }

    // set sizes
    wlr_scene_rect_set_size(bar->background, bar->width, bar->height);

    render_clock(bar);

    if (bar->nbuttons == 0) {
        return;
    }

    struct steppewm_config *cfg = &bar->server->config;
    int pad = cfg->taskbar_button_pad;
    int bh = bar->height - 2 * pad;
    struct steppewm_view *fview = taskbar_focused_view(bar);

    // buttons share the space between the left edge and the clock, capped at the
    // configured width. each button occupies (bw + pad)
    // the row must end a pad before the clock's left edge

    // x coord that clock starts at
    int clock_left = bar->width - bar->clock_w - pad;

    // remove right padding and left padding
    int avail = clock_left - pad - pad;

    // divide by number of buttons to get width
    int bw = (avail - bar->nbuttons * pad) / bar->nbuttons;

    // don't exceed configured max
    if (bw > cfg->taskbar_button_w) {
        bw = cfg->taskbar_button_w;
    }

    // never zero
    if (bw < 1) {
        bw = 1;
    }
    bar->button_w = bw;

    for (int i = 0; i < bar->nbuttons; i++) {
        struct steppewm_task_button *btn = &bar->buttons[i];

        // set top left corner of the button
        int bx = pad + i * (bw + pad);
        wlr_scene_node_set_position(&btn->label->node, bx, pad);

        float *bg;
        if (btn->view == fview) {
            bg = cfg->color_task_active;
        } else if (btn->view->minimized) {
            bg = cfg->color_task_minimized;
        } else {
            bg = cfg->color_task_normal;
        }

        const char *title = btn->view->toplevel->title ? btn->view->toplevel->title : "";
        render_button(btn->label, title, bw, bh, bg, cfg->color_task_text);
    }
}

// redraw taskbar when title changed
static void on_title_changed(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_task_button *btn = wl_container_of(listener, btn, title_changed);
    taskbar_layout(btn->bar);
}

// create taskbar, scene tree, and bg
struct steppewm_taskbar *taskbar_create(struct steppewm_server *server,
                                        struct wlr_output *wlr_output) {
    struct steppewm_taskbar *bar = calloc(1, sizeof(*bar));
    bar->server = server;
    bar->wlr_output = wlr_output;
    bar->height = server->config.taskbar_h;
    bar->tree = wlr_scene_tree_create(&server->scene->tree);
    bar->background =
        wlr_scene_rect_create(bar->tree, 0, bar->height, server->config.color_taskbar_bg);

    // clock label and a timer that ticks it over each minute
    bar->clock = wlr_scene_buffer_create(bar->tree, nullptr);
    struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
    bar->clock_timer = wl_event_loop_add_timer(loop, clock_tick, bar);
    wl_event_source_timer_update(bar->clock_timer, 1);

    return bar;
}

// destroy and free bar
void taskbar_destroy(struct steppewm_taskbar *bar) {
    if (bar->clock_timer) {
        wl_event_source_remove(bar->clock_timer);
    }
    for (int i = 0; i < bar->nbuttons; i++) {
        wl_list_remove(&bar->buttons[i].title_changed.link);
    }
    free(bar);
}

// create new taskbar button for new window
void taskbar_view_added(struct steppewm_taskbar *bar, struct steppewm_view *view) {
    if (bar->nbuttons >= TASKBAR_MAX) {
        return;
    }

    struct steppewm_task_button *btn = &bar->buttons[bar->nbuttons++];
    btn->bar = bar;
    btn->view = view;
    btn->label = wlr_scene_buffer_create(bar->tree, nullptr);

    btn->title_changed.notify = on_title_changed;
    wl_signal_add(&view->toplevel->events.set_title, &btn->title_changed);

    taskbar_layout(bar);
}

// remove window's taskbar button and redraw the taskbar
void taskbar_view_removed(struct steppewm_taskbar *bar, struct steppewm_view *view) {
    int idx = -1;
    for (int i = 0; i < bar->nbuttons; i++) {
        button if (bar->buttons[i].view == view) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return;
    }

    // remove listeners for addresses higher
    for (int i = idx; i < bar->nbuttons; i++) {
        wl_list_remove(&bar->buttons[i].title_changed.link);
    }
    wlr_scene_node_destroy(&bar->buttons[idx].label->node);

    for (int i = idx; i < bar->nbuttons - 1; i++) {
        bar->buttons[i] = bar->buttons[i + 1];
    }
    bar->nbuttons--;

    // reregister listeners for buttons that shifted to new addresses
    for (int i = idx; i < bar->nbuttons; i++) {
        bar->buttons[i].title_changed.notify = on_title_changed;
        wl_signal_add(&bar->buttons[i].view->toplevel->events.set_title,
                      &bar->buttons[i].title_changed);
    }

    taskbar_layout(bar);
}

// refresh taskbar
void taskbar_refresh(struct steppewm_taskbar *bar) {
    taskbar_layout(bar);
}

// update size and position
void taskbar_update_geometry(struct steppewm_taskbar *bar) {
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

// return corresponding steppewm_view depending on which taskbar button is at the xy position
struct steppewm_view *taskbar_view_at(struct steppewm_taskbar *bar, double x, double y) {
    // safety
    if (bar->nbuttons == 0 || bar->width <= 0) {
        return nullptr;
    }
    if (y < bar->y || y >= bar->y + bar->height) {
        return nullptr;
    }
    if (x < bar->x || x >= bar->x + bar->width) {
        return nullptr;
    }

    // find relative x position to taskbar
    int lx = (int) (x - bar->x);
    struct steppewm_config *cfg = &bar->server->config;
    int pad = cfg->taskbar_button_pad;
    int bw = bar->button_w > 0 ? bar->button_w : cfg->taskbar_button_w;

    // find which button index we are on
    int i = (lx - pad) / (bw + pad);
    if (i < 0 || i >= bar->nbuttons) {
        return nullptr;
    }
    // reject clicks in the gap or past the button's right edge
    if (lx - pad - i * (bw + pad) >= bw) {
        return nullptr;
    }
    return bar->buttons[i].view;
}
