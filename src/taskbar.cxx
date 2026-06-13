
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

#include <climits>
#include <unistd.h>
#ifdef __linux__
#include <sys/inotify.h>
#endif

#include <cairo/cairo.h>
#include <drm_fourcc.h>

#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__OpenBSD__)
#include <sys/sensors.h>
#include <sys/sysctl.h>
#endif

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

#include "output.hxx"
#include "view.hxx"
#include "volume.hxx"

using namespace steppewm;

wlr_xdg_toplevel_icon_v1_buffer* taskbar::pick_icon_buffer(wlr_xdg_toplevel_icon_v1* icon,
                                                           int target_size) {
    struct wlr_xdg_toplevel_icon_v1_buffer* best = nullptr;
    int best_diff = __INT_MAX__;
    struct wlr_xdg_toplevel_icon_v1_buffer* ibuf;
    wl_list_for_each(ibuf, &icon->buffers, link) {
        int diff = abs(ibuf->buffer->width - target_size);
        if (diff < best_diff) {
            best = ibuf;
            best_diff = diff;
        }
    }
    return best;
}

// render button into cairo then draw it
void taskbar::render_button(struct wlr_scene_buffer* scene_buf, const char* text,
                            struct wlr_xdg_toplevel_icon_v1* icon, bool pinned, int w, int h,
                            float bg[4], float fg[4]) {
    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    int text_left = 0;

    // xdg-toplevel-icon protocol
    if (icon && !wl_list_empty(&icon->buffers)) {
        if (const int icon_size = h - 4; icon_size > 0) {
            if (const wlr_xdg_toplevel_icon_v1_buffer* ibuf = pick_icon_buffer(icon, icon_size)) {
                void* data;
                uint32_t format;
                size_t stride;
                if (wlr_buffer_begin_data_ptr_access(ibuf->buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ,
                                                     &data, &format, &stride)) {
                    cairo_format_t cairo_fmt;
                    if (format == DRM_FORMAT_ARGB8888) {
                        cairo_fmt = CAIRO_FORMAT_ARGB32;
                    } else if (format == DRM_FORMAT_XRGB8888) {
                        cairo_fmt = CAIRO_FORMAT_RGB24;
                    } else {
                        wlr_buffer_end_data_ptr_access(ibuf->buffer);
                        goto draw_text;
                    }

                    cairo_surface_t* icon_surface = cairo_image_surface_create_for_data(
                        static_cast<unsigned char*>(data), cairo_fmt, ibuf->buffer->width,
                        ibuf->buffer->height, static_cast<int>(stride));
                    const double scale = static_cast<double>(icon_size) / ibuf->buffer->width;
                    cairo_save(cr);
                    cairo_translate(cr, 2, 2);
                    cairo_scale(cr, scale, scale);
                    cairo_set_source_surface(cr, icon_surface, 0, 0);
                    cairo_paint(cr);
                    cairo_restore(cr);

                    cairo_surface_destroy(icon_surface);
                    wlr_buffer_end_data_ptr_access(ibuf->buffer);
                    text_left = icon_size + 4;
                }
            }
        }
    }

draw_text:
    if (text && text[0]) {
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, h * 0.55);
        cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, text, &ext);
        double tx;
        if (text_left > 0) {
            tx = text_left;
        } else {
            tx = (w - ext.width) / 2.0 - ext.x_bearing;
        }
        if (tx < 4.0) {
            tx = 4.0;
        }
        double ty = h / 2.0 - ext.y_bearing - ext.height / 2.0;
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, text);
    }

    if (pinned) {
        const double r = h * 0.15;
        cairo_arc(cr, w - r - 2, r + 2, r, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
        cairo_fill(cr);
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

bool bat_info::read_battery(const char* battery_path) {
    if (!battery_path[0]) {
        return false;
    }

#if defined(__linux__)
    const char* path = battery_path;
    if (strcmp(path, "auto") == 0) {
        return false;
    }

    char cap_path[256], status_path[256];
    snprintf(cap_path, sizeof(cap_path), "%s/capacity", path);
    snprintf(status_path, sizeof(status_path), "%s/status", path);

    FILE* f = fopen(cap_path, "r");
    if (!f) {
        return false;
    }
    capacity = 0;
    fscanf(f, "%d", &capacity);
    fclose(f);

    char status[32] = "";
    f = fopen(status_path, "r");
    if (f) {
        fscanf(f, "%31s", status);
        fclose(f);
    }

    if (strcmp(status, "Charging") == 0) {
        state = bat_state::CHARGING;
    } else if (strcmp(status, "Full") == 0) {
        state = bat_state::FULL;
    } else if (strcmp(status, "Discharging") == 0) {
        state = bat_state::DISCHARGING;
    } else {
        state = bat_state::UNKNOWN;
    }
    return true;

#elif defined(__FreeBSD__) || defined(__DragonFly__)
    int life = -1, state = 0;
    size_t len;

    len = sizeof(life);
    if (sysctlbyname("hw.acpi.battery.life", &life, &len, nullptr, 0) != 0 || life < 0) {
        return false;
    }
    capacity = life;

    len = sizeof(state);
    sysctlbyname("hw.acpi.battery.state", &state, &len, nullptr, 0);
    if (state == 2) {
        state = bat_state::CHARGING;
    } else if (state == 1) {
        state = bat_state::DISCHARGING;
    } else {
        state = bat_state::FULL;
    }
    return true;

#elif defined(__OpenBSD__)
    int mib[5] = {CTL_HW, HW_SENSORS, 0, 0, 0};
    struct sensordev sd;
    size_t sdlen = sizeof(sd);
    bool found = false;

    for (int dev = 0;; dev++) {
        mib[2] = dev;
        sdlen = sizeof(sd);
        if (sysctl(mib, 3, &sd, &sdlen, nullptr, 0) == -1) {
            break;
        }
        if (strncmp(sd.xname, "acpibat", 7) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }

    struct sensor s;
    size_t slen = sizeof(s);

    int64_t remaining = 0, full_cap = 1;

    mib[3] = SENSOR_WATTHOUR;
    mib[4] = 4;
    slen = sizeof(s);
    if (sysctl(mib, 5, &s, &slen, nullptr, 0) == 0) {
        remaining = s.value;
    }

    mib[4] = 3;
    slen = sizeof(s);
    if (sysctl(mib, 5, &s, &slen, nullptr, 0) == 0 && s.value > 0) {
        full_cap = s.value;
    }

    capacity = static_cast<int>((remaining * 100) / full_cap);
    if (capacity > 100) {
        capacity = 100;
    }
    if (capacity < 0) {
        capacity = 0;
    }

    mib[3] = SENSOR_INTEGER;
    mib[4] = 0;
    slen = sizeof(s);
    if (sysctl(mib, 5, &s, &slen, nullptr, 0) == 0) {
        if (s.value == 2) {
            state = bat_state::CHARGING;
        } else if (s.value == 1) {
            state = bat_state::DISCHARGING;
        } else {
            state = bat_state::FULL;
        }
    } else {
        state = bat_state::UNKNOWN;
    }
    return true;

#else
    return false;
#endif
}

// pretty self explanatory
void taskbar::render_battery() {
    if (width_ <= 0 || height_ <= 0 || !battery_) {
        return;
    }

    bat_info info{};
    if (!info.read_battery(srv_->cfg.battery_path)) {
        battery_w_ = 0;
        wlr_scene_node_set_enabled(&battery_->node, false);
        return;
    }

    char text[32];
    auto symbol = "";
    if (info.state == bat_state::CHARGING) {
        symbol = "+";
    } else if (info.state == bat_state::FULL) {
        symbol = "";
    }
    snprintf(text, sizeof(text), "BAT %d%%%s", info.capacity, symbol);

    config* cfg = &srv_->cfg;
    const int pad = cfg->taskbar_button_pad;
    const int h = height_ - 2 * pad;
    if (h <= 0) {
        return;
    }
    const double font_size = h * 0.55;

    const cairo_text_extents_t ext = paint::text_extents(text, font_size);
    const int w = static_cast<int>(ext.width) + h;
    if (w <= 0) {
        return;
    }
    battery_w_ = w;

    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    const float* bg = cfg->color_task_normal;
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    const float* fg = cfg->color_task_text;
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
    const double tx = (w - ext.width) / 2.0 - ext.x_bearing;
    const double ty = h / 2.0 - ext.y_bearing - ext.height / 2.0;
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, text);

    canvas.commit(battery_);
    wlr_scene_node_set_enabled(&battery_->node, true);

    const int x = width_ - clock_w_ - pad - layout_ind_w_ - pad - w - pad;
    wlr_scene_node_set_position(&battery_->node, x, pad);
}

static int read_brightness(const char* backlight_path) {
    if (!backlight_path[0]) {
        return -1;
    }

#if defined(__linux__)
    if (strcmp(backlight_path, "auto") == 0) {
        return -1;
    }

    char path[256];
    int cur = 0, max = 0;

    snprintf(path, sizeof(path), "%s/brightness", backlight_path);
    FILE* f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    fscanf(f, "%d", &cur);
    fclose(f);

    snprintf(path, sizeof(path), "%s/max_brightness", backlight_path);
    f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    fscanf(f, "%d", &max);
    fclose(f);

    if (max <= 0) {
        return -1;
    }
    return (cur * 100 + max / 2) / max;

#elif defined(__FreeBSD__) || defined(__DragonFly__)
    int level = -1;
    size_t len = sizeof(level);
    if (sysctlbyname("hw.acpi.video.lcd0.brightness", &level, &len, nullptr, 0) != 0) {
        return -1;
    }
    return level;

#elif defined(__OpenBSD__)
    int mib[5] = {CTL_HW, HW_SENSORS, 0, 0, 0};
    struct sensordev sd;
    size_t sdlen;
    bool found = false;

    for (int dev = 0;; dev++) {
        mib[2] = dev;
        sdlen = sizeof(sd);
        if (sysctl(mib, 3, &sd, &sdlen, nullptr, 0) == -1) {
            break;
        }
        if (strncmp(sd.xname, "acpivout", 8) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        return -1;
    }

    struct sensor s;
    size_t slen = sizeof(s);
    mib[3] = SENSOR_PERCENT;
    mib[4] = 0;
    if (sysctl(mib, 5, &s, &slen, nullptr, 0) != 0) {
        return -1;
    }
    return static_cast<int>(s.value / 1000);

#else
    return -1;
#endif
}

void taskbar::render_brightness() {
    if (width_ <= 0 || height_ <= 0 || !brightness_) {
        return;
    }

    const int pct = read_brightness(srv_->cfg.backlight_path);
    if (pct < 0) {
        brightness_w_ = 0;
        wlr_scene_node_set_enabled(&brightness_->node, false);
        return;
    }

    char text[32];
    snprintf(text, sizeof(text), "BRI %d%%", pct);

    const config* cfg = &srv_->cfg;
    const int pad = cfg->taskbar_button_pad;
    const int h = height_ - 2 * pad;
    if (h <= 0) {
        return;
    }
    const double font_size = h * 0.55;

    const cairo_text_extents_t ext = paint::text_extents(text, font_size);
    const int w = static_cast<int>(ext.width) + h;
    if (w <= 0) {
        return;
    }
    brightness_w_ = w;

    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    const float* bg = cfg->color_task_normal;
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    const float* fg = cfg->color_task_text;
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
    const double tx = (w - ext.width) / 2.0 - ext.x_bearing;
    const double ty = h / 2.0 - ext.y_bearing - ext.height / 2.0;
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, text);

    canvas.commit(brightness_);
    wlr_scene_node_set_enabled(&brightness_->node, true);

    int ix = width_ - clock_w_ - pad - layout_ind_w_ - pad;
    if (battery_w_ > 0) {
        ix -= battery_w_ + pad;
    }
    ix -= w + pad;
    wlr_scene_node_set_position(&brightness_->node, ix, pad);
}

void taskbar::render_volume() {
    if (width_ <= 0 || height_ <= 0 || !volume_) {
        return;
    }

#ifdef HAVE_LIBPULSE
    const auto* vm = static_cast<volume_monitor*>(srv_->vol_mon);
    if (!vm || vm->volume() < 0) {
        volume_w_ = 0;
        wlr_scene_node_set_enabled(&volume_->node, false);
        return;
    }

    char text[32];
    if (vm->muted()) {
        snprintf(text, sizeof(text), "VOL MUTE");
    } else {
        snprintf(text, sizeof(text), "VOL %d%%", vm->volume());
    }

    config* cfg = &srv_->cfg;
    const int pad = cfg->taskbar_button_pad;
    const int h = height_ - 2 * pad;
    if (h <= 0) {
        return;
    }
    const double font_size = h * 0.55;

    const cairo_text_extents_t ext = paint::text_extents(text, font_size);
    const int w = static_cast<int>(ext.width) + h;
    if (w <= 0) {
        return;
    }
    volume_w_ = w;

    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    const float* bg = cfg->color_task_normal;
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
    cairo_paint(cr);

    const float* fg = cfg->color_task_text;
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
    const double tx = (w - ext.width) / 2.0 - ext.x_bearing;
    const double ty = h / 2.0 - ext.y_bearing - ext.height / 2.0;
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, text);

    canvas.commit(volume_);
    wlr_scene_node_set_enabled(&volume_->node, true);

    int vx = width_ - clock_w_ - pad - layout_ind_w_ - pad;
    if (battery_w_ > 0) {
        vx -= battery_w_ + pad;
    }
    if (brightness_w_ > 0) {
        vx -= brightness_w_ + pad;
    }
    vx -= w + pad;
    wlr_scene_node_set_position(&volume_->node, vx, pad);
#else
    volume_w_ = 0;
    wlr_scene_node_set_enabled(&volume_->node, false);
#endif
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
            out[n] = static_cast<char>(toupper(static_cast<unsigned char>(tok[n])));
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
    struct wlr_xdg_surface* xdg = wlr_xdg_surface_try_from_wlr_surface(surf);
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
    render_battery();
    render_brightness();
    render_volume();

    config* cfg = &srv_->cfg;
    const int pad = cfg->taskbar_button_pad;
    int button_h = height_ - 2 * pad;
    if (button_h < 1) {
        button_h = 1;
    }
    const int current = srv_->current_workspace;

    // workspace indicator buttons
    // width is the same as height since it is square
    const int ws_button_w = button_h;
    ws_button_w_ = ws_button_w;
    int cursor_x = pad;

    // draw each button
    for (int i = 0; i < num_workspaces; i++) {
        wlr_scene_node_set_position(&ws_labels_[i]->node, cursor_x, pad);
        char num[4];
        snprintf(num, sizeof(num), "%d", i + 1);
        float* bg = (i == current) ? cfg->color_task_active : cfg->color_task_normal;
        render_button(ws_labels_[i], num, nullptr, false, ws_button_w, button_h, bg,
                      cfg->color_task_text);
        cursor_x += ws_button_w + pad;
    }

    // first task button starts just past the indicators
    const int task_row_left = cursor_x;

    // count windows that live on the current workspace
    int visible_count = 0;
    for (const auto& btn : buttons_) {
        if (btn->v->pinned || btn->v->workspace == current) {
            visible_count++;
        }
    }

    // if there's no windows on the current workspace, hide all of them
    if (visible_count == 0) {
        for (const auto& btn : buttons_) {
            wlr_scene_node_set_enabled(&btn->label->node, false);
        }
        return;
    }

    const view* fv = focused_view();

    // task row ends before the right-side indicators
    int right_limit = width_ - clock_w_ - pad - layout_ind_w_ - pad;
    if (battery_w_ > 0) {
        right_limit -= battery_w_ + pad;
    }
    if (brightness_w_ > 0) {
        right_limit -= brightness_w_ + pad;
    }
    if (volume_w_ > 0) {
        right_limit -= volume_w_ + pad;
    }
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
    for (const auto& btn : buttons_) {
        // skip and hide windows on other workspaces
        if (!btn->v->pinned && btn->v->workspace != current) {
            wlr_scene_node_set_enabled(&btn->label->node, false);
            continue;
        }
        wlr_scene_node_set_enabled(&btn->label->node, true);

        // set top left corner of the button
        int button_x = task_row_left + slot * (button_w + pad);
        wlr_scene_node_set_position(&btn->label->node, button_x, pad);

        float* bg;
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

        const char* title = btn->v->toplevel->title ? btn->v->toplevel->title : "";
        render_button(btn->label, title, btn->v->icon, btn->v->pinned, button_w, button_h, bg,
                      cfg->color_task_text);
        slot++;
    }

    if (has_visible_urgent) {
        wl_event_source_timer_update(urgent_timer_, 500);
    } else {
        urgent_flash_on_ = true;
    }
}

// create taskbar, scene tree, and bg
taskbar::taskbar(server* s, struct wlr_output* wlr_output) {
    srv_ = s;
    wlr_output_ = wlr_output;
    height_ = s->cfg.taskbar_h;
    tree_ = wlr_scene_tree_create(&s->scene->tree);
    background_ = wlr_scene_rect_create(tree_, 0, height_, s->cfg.color_taskbar_bg);

    for (int i = 0; i < num_workspaces; i++) {
        ws_labels_[i] = wlr_scene_buffer_create(tree_, nullptr);
    }

    // status indicators
    battery_ = wlr_scene_buffer_create(tree_, nullptr);
    brightness_ = wlr_scene_buffer_create(tree_, nullptr);
    volume_ = wlr_scene_buffer_create(tree_, nullptr);

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
void taskbar::view_removed(view* v) {
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
view* taskbar::view_at(double x, double y) {
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
    int local_x = (int) (x - x_);

    config* cfg = &srv_->cfg;
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
        if (!btn->v->pinned && btn->v->workspace != current) {
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
    config* cfg = &srv_->cfg;
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
void taskbar::refresh_taskbars(server* s) {
    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        if (out->taskbar) {
            out->taskbar->refresh();
        }
    }
}

// called every two seconds by the wlroots event in taskbar::init_monitors
static int on_indicator_poll(void* data) {
    auto* s = static_cast<server*>(data);
    taskbar::refresh_taskbars(s);
    wl_event_source_timer_update(s->indicator_timer, 2000);
    return 0;
}

// called when the brightness file is updated
#ifdef __linux__
static int on_brightness_inotify(int fd, uint32_t, void* data) {
    char buf[sizeof(inotify_event) + NAME_MAX + 1];

    // drain the inotify queue
    while (read(fd, buf, sizeof(buf)) > 0) {
    }

    // refresh indicators
    taskbar::refresh_taskbars(static_cast<server*>(data));
    return 0;
}
#endif

// create monitors for the taskbar indicators
void taskbar::init_monitors(server* s) {
    wl_event_loop* loop = wl_display_get_event_loop(s->display);
    s->brightness_watch_fd = -1;

    // polling on all platforms
    s->indicator_timer = wl_event_loop_add_timer(loop, on_indicator_poll, s);
    wl_event_source_timer_update(s->indicator_timer, 2000);

    // linux specific stuff
#ifdef __linux__
    if (s->cfg.backlight_path[0] && strcmp(s->cfg.backlight_path, "auto") != 0) {
        // create inotify fd
        int ifd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);

        if (ifd >= 0) {
            char path[256];
            // watch this path for modifications
            snprintf(path, sizeof(path), "%s/brightness", s->cfg.backlight_path);

            if (inotify_add_watch(ifd, path, IN_MODIFY) >= 0) {
                s->brightness_watch_fd = ifd;

                // call on_brighntess_inotify whenever the inotify fd changes
                s->brightness_source =
                    wl_event_loop_add_fd(loop, ifd, WL_EVENT_READABLE, on_brightness_inotify, s);
            } else {
                close(ifd);
            }
        }
    }
#endif
}

// destroy indicator monitors
void taskbar::fini_monitors(const server* s) {
    if (s->indicator_timer) {
        wl_event_source_remove(s->indicator_timer);
    }
    if (s->brightness_source) {
        wl_event_source_remove(s->brightness_source);
    }
    if (s->brightness_watch_fd >= 0) {
        close(s->brightness_watch_fd);
    }
}
