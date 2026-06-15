
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

#include "osd.hxx"
#include "output.hxx"
#include "tray.hxx"
#include "view.hxx"
#include "volume.hxx"

using namespace steppewm;

static cairo_surface_t* try_icon_name(const char* name, const int target_size) {
    static const int sizes[] = {512, 256, 128, 96, 72, 64, 48, 36, 32, 24, 22, 16};
    char path[512];

    // find the best match
    int best_size = 0;
    int best_diff = INT_MAX;
    for (const int s : sizes) {
        snprintf(path, sizeof(path), "/usr/share/icons/hicolor/%dx%d/apps/%s.png", s, s, name);
        if (access(path, R_OK) == 0) {
            const int diff = abs(s - target_size);
            if (diff < best_diff) {
                best_diff = diff;
                best_size = s;
            }
        }
    }

    // return the best size if we got one
    if (best_size > 0) {
        snprintf(path, sizeof(path), "/usr/share/icons/hicolor/%dx%d/apps/%s.png", best_size,
                 best_size, name);
        cairo_surface_t* surf = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
            return surf;
        }
        cairo_surface_destroy(surf);
    }

    // try pixmaps
    snprintf(path, sizeof(path), "/usr/share/pixmaps/%s.png", name);
    if (access(path, R_OK) == 0) {
        cairo_surface_t* surf = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
            return surf;
        }
        cairo_surface_destroy(surf);
    }

    return nullptr;
}

static cairo_surface_t* load_app_icon(const char* app_id, int target_size) {
    cairo_surface_t* surf = try_icon_name(app_id, target_size);
    if (surf) {
        return surf;
    }

    // look up the Icon= field from .desktop files
    char path[512];
    const char* dirs[] = {"/usr/share/applications", "/usr/local/share/applications"};
    for (const char* dir : dirs) {
        snprintf(path, sizeof(path), "%s/%s.desktop", dir, app_id);
        FILE* f = fopen(path, "r");
        if (!f) {
            continue;
        }
        char line[512];
        char icon_name[256] = "";
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "Icon=", 5) == 0) {
                char* nl = strchr(line + 5, '\n');
                if (nl) {
                    *nl = '\0';
                }
                strncpy(icon_name, line + 5, sizeof(icon_name) - 1);
                icon_name[sizeof(icon_name) - 1] = '\0';
                break;
            }
        }
        fclose(f);

        if (icon_name[0]) {
            if (icon_name[0] == '/') {
                surf = cairo_image_surface_create_from_png(icon_name);
                if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
                    return surf;
                }
                cairo_surface_destroy(surf);
            } else {
                surf = try_icon_name(icon_name, target_size);
                if (surf) {
                    return surf;
                }
            }
        }
    }

    return nullptr;
}

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
    brightness_x_ = ix + x_;
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
    volume_x_ = vx + x_;
    wlr_scene_node_set_position(&volume_->node, vx, pad);
#else
    volume_w_ = 0;
    wlr_scene_node_set_enabled(&volume_->node, false);
#endif
}

void taskbar::render_idle_indicator() {
    if (width_ <= 0 || height_ <= 0 || !idle_ind_) {
        return;
    }

    const bool inhibited = srv_->idle_inhibit_manual;
    const char* text = inhibited ? "AWAKE" : "IDLE";

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
    idle_ind_w_ = w;

    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    float* bg = inhibited ? cfg->color_task_active : cfg->color_task_normal;
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

    canvas.commit(idle_ind_);
    wlr_scene_node_set_enabled(&idle_ind_->node, true);

    int ix = width_ - clock_w_ - pad - layout_ind_w_ - pad;
    if (battery_w_ > 0) {
        ix -= battery_w_ + pad;
    }
    if (brightness_w_ > 0) {
        ix -= brightness_w_ + pad;
    }
    if (volume_w_ > 0) {
        ix -= volume_w_ + pad;
    }
    ix -= w + pad;
    idle_ind_x_ = ix + x_;
    wlr_scene_node_set_position(&idle_ind_->node, ix, pad);
}

void taskbar::render_tray() {
#ifdef HAVE_SDBUS
    auto* tray = static_cast<tray_host*>(srv_->tray);

    // guard
    if (!tray || width_ <= 0 || height_ <= 0) {
        for (auto* buf : tray_bufs_) {
            wlr_scene_node_set_enabled(&buf->node, false);
        }
        tray_total_w_ = 0;
        return;
    }

    auto& items = tray->items();

    // if an icon was added, create it
    while (tray_bufs_.size() < items.size()) {
        tray_bufs_.push_back(wlr_scene_buffer_create(tree_, nullptr));
        tray_buf_state_.push_back({});
    }

    // if an icon was removed, remove it
    while (tray_bufs_.size() > items.size()) {
        wlr_scene_node_destroy(&tray_bufs_.back()->node);
        tray_bufs_.pop_back();
        tray_buf_state_.pop_back();
    }

    // if the list of items is empty, ealy exit
    if (items.empty()) {
        tray_total_w_ = 0;
        return;
    }

    config* cfg = &srv_->cfg;
    const int pad = cfg->taskbar_button_pad;
    int icon_size = height_ - 2 * pad;
    if (icon_size < 1) {
        icon_size = 1;
    }

    int base_x = width_ - clock_w_ - pad - layout_ind_w_ - pad;
    if (battery_w_ > 0) {
        base_x -= battery_w_ + pad;
    }
    if (brightness_w_ > 0) {
        base_x -= brightness_w_ + pad;
    }
    if (volume_w_ > 0) {
        base_x -= volume_w_ + pad;
    }
    if (idle_ind_w_ > 0) {
        base_x -= idle_ind_w_ + pad;
    }

    const int n = static_cast<int>(items.size());
    tray_total_w_ = n * (icon_size + pad);
    tray_x_ = x_ + base_x - tray_total_w_;

    // draw each tray icon into each buffer
    for (int i = 0; i < n; i++) {
        const int ix = base_x - (n - i) * (icon_size + pad);
        auto& item = items[i];

        // avoid redrawing for no reason
        bool needs_redraw = tray_buf_state_[i].item != item.get() ||
                            tray_buf_state_[i].gen != item->icon_gen ||
                            tray_buf_state_[i].size != icon_size;

        if (needs_redraw) {
            paint::Canvas canvas(icon_size, icon_size);

            // draw into the canvas
            if (canvas.valid()) {
                cairo_t* cr = canvas.cr();

                const float* bg = cfg->color_task_normal;
                cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
                cairo_paint(cr);

                // if the icon is valid
                if (!item->icon_pixels.empty() && item->icon_width > 0 && item->icon_height > 0) {
                    constexpr int margin = 2;
                    const int draw_size = icon_size - 2 * margin;
                    if (draw_size > 0) {
                        cairo_surface_t* icon_surface = cairo_image_surface_create_for_data(
                            reinterpret_cast<unsigned char*>(item->icon_pixels.data()),
                            CAIRO_FORMAT_ARGB32, item->icon_width, item->icon_height,
                            item->icon_width * 4);

                        const double scale = static_cast<double>(draw_size) /
                                             std::max(item->icon_width, item->icon_height);

                        const int offset_x =
                            margin + (draw_size - static_cast<int>(item->icon_width * scale)) / 2;
                        const int offset_y =
                            margin + (draw_size - static_cast<int>(item->icon_height * scale)) / 2;

                        cairo_save(cr);
                        cairo_translate(cr, offset_x, offset_y);
                        cairo_scale(cr, scale, scale);
                        cairo_set_source_surface(cr, icon_surface, 0, 0);
                        cairo_paint(cr);
                        cairo_restore(cr);
                        cairo_surface_destroy(icon_surface);
                    }
                }

                canvas.commit(tray_bufs_[i]);
                tray_buf_state_[i] = {item.get(), item->icon_gen, icon_size};
            }
        }

        wlr_scene_node_set_position(&tray_bufs_[i]->node, ix, pad);
        wlr_scene_node_set_enabled(&tray_bufs_[i]->node, true);
    }
#else
    tray_total_w_ = 0;
    tray_x_ = 0;
#endif
}

// is the tray at the x and y coord?
int taskbar::tray_at(const double x, const double y) const {
#ifdef HAVE_SDBUS
    if (tray_total_w_ <= 0 || width_ <= 0) {
        return -1;
    }
    if (y < y_ || y >= y_ + height_) {
        return -1;
    }

    const int pad = srv_->cfg.taskbar_button_pad;
    const int icon_size = height_ - 2 * pad;
    if (icon_size < 1) {
        return -1;
    }

    const int local_x = static_cast<int>(x - tray_x_);
    if (local_x < 0 || local_x >= tray_total_w_) {
        return -1;
    }

    const int idx = local_x / (icon_size + pad);
    if (local_x - idx * (icon_size + pad) >= icon_size) {
        return -1;
    }

    auto* tray = static_cast<tray_host*>(srv_->tray);
    if (!tray || idx < 0 || idx >= static_cast<int>(tray->items().size())) {
        return -1;
    }

    return idx;
#else
    (void) x;
    (void) y;
    return -1;
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
    render_idle_indicator();
    render_tray();

    config* cfg = &srv_->cfg;
    const int pad = cfg->taskbar_button_pad;
    int button_h = height_ - 2 * pad;
    if (button_h < 1) {
        button_h = 1;
    }
    const int current = srv_->current_workspace;
    const int npins = cfg->npins;

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

    // sync pin label scene buffers and icon cache to match config
    bool pins_changed = static_cast<int>(pin_labels_.size()) != npins;
    while (static_cast<int>(pin_labels_.size()) < npins) {
        pin_labels_.push_back(wlr_scene_buffer_create(tree_, nullptr));
    }
    while (static_cast<int>(pin_labels_.size()) > npins) {
        wlr_scene_node_destroy(&pin_labels_.back()->node);
        pin_labels_.pop_back();
    }

    if (pins_changed) {
        // remove all
        for (auto* s : pin_icons_) {
            if (s) {
                cairo_surface_destroy(s);
            }
        }
        pin_icons_.clear();
        pin_icons_.resize(npins, nullptr);
        const int target = button_h > 0 ? button_h : 20;

        // redraw each
        for (int i = 0; i < npins; i++) {
            // if a path was specified
            if (cfg->pins[i].icon_path[0]) {
                pin_icons_[i] = cairo_image_surface_create_from_png(cfg->pins[i].icon_path);
                if (cairo_surface_status(pin_icons_[i]) != CAIRO_STATUS_SUCCESS) {
                    cairo_surface_destroy(pin_icons_[i]);
                    pin_icons_[i] = nullptr;
                }
            }

            // if not
            if (!pin_icons_[i]) {
                pin_icons_[i] = load_app_icon(cfg->pins[i].app_id, target);
            }
        }
    }

    // pinned apps then open windows
    slots_.clear();
    std::vector claimed(buttons_.size(), false);

    // for each pinned app
    for (int pin_index = 0; pin_index < npins; pin_index++) {
        task_button* matched_btn = nullptr;
        view* matched_view = nullptr;
        // for each taskbar button
        for (size_t bi = 0; bi < buttons_.size(); bi++) {
            if (claimed[bi]) {
                continue;
            }
            const char* app_id = buttons_[bi]->v->toplevel->app_id;

            // if button app_id matches pin app_id
            if (app_id && strcmp(app_id, cfg->pins[pin_index].app_id) == 0) {
                matched_view = buttons_[bi]->v;
                matched_btn = buttons_[bi].get();
                claimed[bi] = true;
                break;
            }
        }
        slots_.push_back({matched_view, matched_btn, pin_index, 0, 0});
    }

    // for each button that hasn't been matched yet
    for (size_t button_index = 0; button_index < buttons_.size(); button_index++) {
        if (claimed[button_index]) {
            continue;
        }
        view* v = buttons_[button_index]->v;
        if (!v->pinned && v->workspace != current) {
            continue;
        }
        slots_.push_back({v, buttons_[button_index].get(), -1, 0, 0});
    }

    const int total_slots = static_cast<int>(slots_.size());

    // hide everything first
    for (const auto& btn : buttons_) {
        wlr_scene_node_set_enabled(&btn->label->node, false);
    }
    for (auto* pl : pin_labels_) {
        wlr_scene_node_set_enabled(&pl->node, false);
    }

    if (total_slots == 0) {
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
    if (idle_ind_w_ > 0) {
        right_limit -= idle_ind_w_ + pad;
    }
    if (tray_total_w_ > 0) {
        right_limit -= tray_total_w_;
    }
    const int task_row_width = right_limit - pad - task_row_left;

    // calculate how much space pins will take up
    const int pin_button_w = button_h;
    int num_launchers = 0;
    for (auto& display_slot : slots_) {
        // no task button but valid pin index
        if (!display_slot.btn && display_slot.pin_idx >= 0) {
            num_launchers++;
        }
    }
    const int num_window_slots = total_slots - num_launchers;
    const int launcher_space = num_launchers * (pin_button_w + pad);

    // calculate each window button width
    int button_w = 1;
    if (num_window_slots > 0) {
        button_w = (task_row_width - launcher_space - num_window_slots * pad) / num_window_slots;
        if (button_w > cfg->taskbar_button_w) {
            button_w = cfg->taskbar_button_w;
        }
        if (button_w < 1) {
            button_w = 1;
        }
    }
    button_w_ = button_w;

    // draw each slot
    int cx = task_row_left;
    bool has_visible_urgent = false;
    for (auto& ds : slots_) {
        // if it is an open window (matched pin or unpinned window)
        if (ds.btn) {
            ds.x = cx;
            ds.w = button_w;
            wlr_scene_node_set_enabled(&ds.btn->label->node, true);
            wlr_scene_node_set_position(&ds.btn->label->node, cx, pad);

            float* bg;
            if (ds.btn->v == fv) {
                bg = cfg->color_task_active;
            } else if (ds.btn->v->urgent) {
                has_visible_urgent = true;
                bg = urgent_flash_on_ ? cfg->color_task_urgent
                                      : (ds.btn->v->minimized ? cfg->color_task_minimized
                                                              : cfg->color_task_normal);
            } else if (ds.btn->v->minimized) {
                bg = cfg->color_task_minimized;
            } else {
                bg = cfg->color_task_normal;
            }

            const char* title = ds.btn->v->toplevel->title ? ds.btn->v->toplevel->title : "";
            render_button(ds.btn->label, title, ds.btn->v->icon, ds.btn->v->pinned, button_w,
                          button_h, bg, cfg->color_task_text);
            cx += button_w + pad;
        } else if (ds.pin_idx >= 0) { // launcher with no running app
            ds.x = cx;
            ds.w = pin_button_w;
            wlr_scene_buffer* buf = pin_labels_[ds.pin_idx];
            wlr_scene_node_set_enabled(&buf->node, true);
            wlr_scene_node_set_position(&buf->node, cx, pad);

            paint::Canvas canvas(pin_button_w, button_h);
            if (canvas.valid()) {
                cairo_t* cr = canvas.cr();
                float* bg = cfg->color_task_normal;
                cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], bg[3]);
                cairo_paint(cr);

                cairo_surface_t* icon_surf = ds.pin_idx < static_cast<int>(pin_icons_.size())
                                                 ? pin_icons_[ds.pin_idx]
                                                 : nullptr;
                if (icon_surf) {
                    const int icon_size = button_h - 4;
                    if (icon_size > 0) {
                        const double scale = static_cast<double>(icon_size) /
                                             std::max(cairo_image_surface_get_width(icon_surf),
                                                      cairo_image_surface_get_height(icon_surf));
                        const int iw =
                            static_cast<int>(cairo_image_surface_get_width(icon_surf) * scale);
                        const int ih =
                            static_cast<int>(cairo_image_surface_get_height(icon_surf) * scale);
                        cairo_save(cr);
                        cairo_translate(cr, (pin_button_w - iw) / 2.0, (button_h - ih) / 2.0);
                        cairo_scale(cr, scale, scale);
                        cairo_set_source_surface(cr, icon_surf, 0, 0);
                        cairo_paint(cr);
                        cairo_restore(cr);
                    }
                } else {
                    // fallback is the first character of the app_id
                    float* fg = cfg->color_task_text;
                    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                                           CAIRO_FONT_WEIGHT_NORMAL);
                    cairo_set_font_size(cr, button_h * 0.55);
                    cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], fg[3]);
                    char ch[2] = {static_cast<char>(toupper(
                                      static_cast<unsigned char>(cfg->pins[ds.pin_idx].app_id[0]))),
                                  '\0'};
                    cairo_text_extents_t ext;
                    cairo_text_extents(cr, ch, &ext);
                    double tx = (pin_button_w - ext.width) / 2.0 - ext.x_bearing;
                    double ty = button_h / 2.0 - ext.y_bearing - ext.height / 2.0;
                    cairo_move_to(cr, tx, ty);
                    cairo_show_text(cr, ch);
                }
                canvas.commit(buf);
            }
            cx += pin_button_w + pad;
        }
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
    tree_destroy_.connect(&tree_->node.events.destroy, [this](void*) {
        tree_destroy_.disconnect();
        tree_ = nullptr;
    });
    background_ = wlr_scene_rect_create(tree_, 0, height_, s->cfg.color_taskbar_bg);

    for (auto& ws_label : ws_labels_) {
        ws_label = wlr_scene_buffer_create(tree_, nullptr);
    }

    // status indicators
    battery_ = wlr_scene_buffer_create(tree_, nullptr);
    brightness_ = wlr_scene_buffer_create(tree_, nullptr);
    volume_ = wlr_scene_buffer_create(tree_, nullptr);
    idle_ind_ = wlr_scene_buffer_create(tree_, nullptr);

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
    for (auto* s : pin_icons_) {
        if (s) {
            cairo_surface_destroy(s);
        }
    }
    if (clock_timer_) {
        wl_event_source_remove(clock_timer_);
    }
    if (urgent_timer_) {
        wl_event_source_remove(urgent_timer_);
    }
    if (tree_) {
        wlr_scene_node_destroy(&tree_->node);
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
    if (slots_.empty() || width_ <= 0) {
        return nullptr;
    }
    if (y < y_ || y >= y_ + height_) {
        return nullptr;
    }
    if (x < x_ || x >= x_ + width_) {
        return nullptr;
    }

    const int local_x = static_cast<int>(x - x_);
    for (const auto& ds : slots_) {
        if (local_x >= ds.x && local_x < ds.x + ds.w) {
            return ds.v;
        }
    }
    return nullptr;
}

// return pin index if click is on an unmatched pinned app launcher
int taskbar::pin_at(double x, double y) const {
    if (slots_.empty() || width_ <= 0) {
        return -1;
    }
    if (y < y_ || y >= y_ + height_) {
        return -1;
    }
    if (x < x_ || x >= x_ + width_) {
        return -1;
    }

    const int local_x = static_cast<int>(x - x_);
    for (const auto& ds : slots_) {
        if (local_x >= ds.x && local_x < ds.x + ds.w) {
            if (ds.v == nullptr && ds.pin_idx >= 0) {
                return ds.pin_idx;
            }
            return -1;
        }
    }
    return -1;
}

int taskbar::workspace_at(double x, double y) const {
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

bool taskbar::idle_inhibit_at(const double x, const double y) const {
    // if the indicator doesn't exist yet
    if (idle_ind_w_ <= 0 || width_ <= 0) {
        return false;
    }
    // if it's obviously not hit
    if (y < y_ || y >= y_ + height_) {
        return false;
    }

    const int pad = srv_->cfg.taskbar_button_pad;
    const int h = height_ - 2 * pad;
    const bool in_x = x >= idle_ind_x_ && x < idle_ind_x_ + idle_ind_w_;
    const bool in_y = y >= y_ + pad && y < y_ + pad + h;
    return in_x && in_y;
}

bool taskbar::brightness_at(const double x, const double y) const {
    if (brightness_w_ <= 0 || width_ <= 0) {
        return false;
    }
    if (y < y_ || y >= y_ + height_) {
        return false;
    }
    const int pad = srv_->cfg.taskbar_button_pad;
    const int h = height_ - 2 * pad;
    const bool in_x = x >= brightness_x_ && x < brightness_x_ + brightness_w_;
    const bool in_y = y >= y_ + pad && y < y_ + pad + h;
    return in_x && in_y;
}

bool taskbar::volume_at(const double x, const double y) const {
    if (volume_w_ <= 0 || width_ <= 0) {
        return false;
    }
    if (y < y_ || y >= y_ + height_) {
        return false;
    }
    const int pad = srv_->cfg.taskbar_button_pad;
    const int h = height_ - 2 * pad;
    const bool in_x = x >= volume_x_ && x < volume_x_ + volume_w_;
    const bool in_y = y >= y_ + pad && y < y_ + pad + h;
    return in_x && in_y;
}

void taskbar::refresh_taskbars(server* s) {
    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        if (out->output_taskbar) {
            out->output_taskbar->refresh();
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

    auto* s = static_cast<server*>(data);
    taskbar::refresh_taskbars(s);
    if (s->osd_overlay) {
        int pct = read_brightness(s->cfg.backlight_path);
        if (pct >= 0) {
            char text[32];
            snprintf(text, sizeof(text), "BRI %d%%", pct);
            s->osd_overlay->show(text);
        }
    }
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
