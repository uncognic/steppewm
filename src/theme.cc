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

#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#ifdef HAVE_LIBRSVG
#include <librsvg/rsvg.h>
#endif

using namespace steppewm;

theme::theme() = default;

theme::~theme() {
    unload();
}

void theme::unload() {
    for (auto &bs: buttons_) {
        if (bs.normal) {
            cairo_surface_destroy(bs.normal);
            bs.normal = nullptr;
        }
        if (bs.active) {
            cairo_surface_destroy(bs.active);
            bs.active = nullptr;
        }
        if (bs.hover) {
            cairo_surface_destroy(bs.hover);
            bs.hover = nullptr;
        }
    }
    auto free_surf = [](cairo_surface_t *&s) {
        if (s) {
            cairo_surface_destroy(s);
            s = nullptr;
        }
    };
    free_surf(titlebar_active_);
    free_surf(titlebar_inactive_);
    free_surf(border_left_);
    free_surf(border_right_);
    free_surf(border_bottom_);
    free_surf(taskbar_bg_);
    free_surf(taskbutton_);
    free_surf(taskbutton_active_);
    free_surf(taskbutton_minimized_);
    free_surf(workspace_button_);
    free_surf(workspace_button_active_);
}

#ifdef HAVE_LIBRSVG
static cairo_surface_t *load_svg(const char *path) {
    GError *err = nullptr;
    RsvgHandle *handle = rsvg_handle_new_from_file(path, &err);
    if (!handle) {
        if (err) g_error_free(err);
        return nullptr;
    }
    constexpr int sz = 128;
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sz, sz);
    cairo_t *cr = cairo_create(surf);
    constexpr RsvgRectangle viewport = {0, 0, sz, sz};
    rsvg_handle_render_document(handle, cr, &viewport, nullptr);
    cairo_destroy(cr);
    g_object_unref(handle);
    return surf;
}
#endif

// attempt to load image override of element textures
cairo_surface_t *theme::try_load(const char *dir, const char *stem) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", dir, stem);
    if (access(path, R_OK) == 0) {
        cairo_surface_t *s = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) {
            return s;
        }
        cairo_surface_destroy(s);
    }
#ifdef HAVE_LIBRSVG
    snprintf(path, sizeof(path), "%s/%s.svg", dir, stem);
    if (access(path, R_OK) == 0) {
        return load_svg(path);
    }
#endif
    return nullptr;
}

void theme::load(const char *theme_dir) {
    unload();
    if (!theme_dir || !theme_dir[0]) {
        return;
    }

    static const char *btn_stems[][3] = {
        {"close", "close_active", "close_hover"},
        {"maximize", "maximize_active", "maximize_hover"},
        {"minimize", "minimize_active", "minimize_hover"},
    };
    for (int i = 0; i < 3; i++) {
        buttons_[i].normal = try_load(theme_dir, btn_stems[i][0]);
        buttons_[i].active = try_load(theme_dir, btn_stems[i][1]);
        buttons_[i].hover = try_load(theme_dir, btn_stems[i][2]);
    }

    titlebar_active_ = try_load(theme_dir, "titlebar");
    titlebar_inactive_ = try_load(theme_dir, "titlebar_inactive");

    border_left_ = try_load(theme_dir, "border_left");
    border_right_ = try_load(theme_dir, "border_right");
    border_bottom_ = try_load(theme_dir, "border_bottom");

    taskbar_bg_ = try_load(theme_dir, "taskbar_bg");
    taskbutton_ = try_load(theme_dir, "taskbutton");
    taskbutton_active_ = try_load(theme_dir, "taskbutton_active");
    taskbutton_minimized_ = try_load(theme_dir, "taskbutton_minimized");
    workspace_button_ = try_load(theme_dir, "workspace");
    workspace_button_active_ = try_load(theme_dir, "workspace_active");
}

void theme::tile_surface(cairo_t *cr, cairo_surface_t *surf, const int w, const int h) {
    const int ph = cairo_image_surface_get_height(surf);
    const double sy = static_cast<double>(h) / ph;
    cairo_save(cr);
    cairo_scale(cr, 1.0, sy);
    cairo_pattern_t *pat = cairo_pattern_create_for_surface(surf);
    cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
    cairo_set_source(cr, pat);
    cairo_rectangle(cr, 0, 0, w, ph);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);
    cairo_restore(cr);
}

void theme::paint_taskbar_bg(cairo_t *cr, const int w, const int h, const float *fallback,
                             const float *accent) const {
    if (taskbar_bg_) {
        tile_surface(cr, taskbar_bg_, w, h);
    } else {
        cairo_set_source_rgba(cr, fallback[0], fallback[1], fallback[2], fallback[3]);
        cairo_paint(cr);
    }
    if (accent && accent[3] > 0.0f) {
        cairo_set_source_rgba(cr, accent[0], accent[1], accent[2], accent[3]);
        cairo_rectangle(cr, 0, 0, w, 1);
        cairo_fill(cr);
    }
}

static void stretch_surface(cairo_t *cr, cairo_surface_t *surf, const int w, const int h) {
    const auto sx = static_cast<double>(w) / cairo_image_surface_get_width(surf);
    const auto sy = static_cast<double>(h) / cairo_image_surface_get_height(surf);
    cairo_save(cr);
    cairo_scale(cr, sx, sy);
    cairo_set_source_surface(cr, surf, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

void theme::paint_task_button(cairo_t *cr, const int w, const int h, const bool active,
                              const bool minimized, const float *fallback) const {
    cairo_surface_t *pixmap = nullptr;
    if (active && taskbutton_active_) {
        pixmap = taskbutton_active_;
    } else if (minimized && taskbutton_minimized_) {
        pixmap = taskbutton_minimized_;
    } else if (taskbutton_) {
        pixmap = taskbutton_;
    }

    if (pixmap) {
        stretch_surface(cr, pixmap, w, h);
    } else {
        cairo_set_source_rgba(cr, fallback[0], fallback[1], fallback[2], fallback[3]);
        cairo_paint(cr);
    }
}

void theme::paint_workspace_button(cairo_t *cr, const int w, const int h, const bool active,
                                   const float *fallback) const {
    cairo_surface_t *pixmap = active ? workspace_button_active_ : workspace_button_;
    if (pixmap) {
        stretch_surface(cr, pixmap, w, h);
    } else {
        cairo_set_source_rgba(cr, fallback[0], fallback[1], fallback[2], fallback[3]);
        cairo_paint(cr);
    }
}

static int surf_w(cairo_surface_t *s) {
    return s ? cairo_image_surface_get_width(s) : 0;
}

static int surf_h(cairo_surface_t *s) {
    return s ? cairo_image_surface_get_height(s) : 0;
}

theme::suggested_dims theme::get_dims() const {
    suggested_dims d;
    int max_h = 0;
    for (const auto &bs: buttons_) {
        cairo_surface_t *s = bs.active ? bs.active : bs.normal;
        if (s) {
            int h = surf_h(s);
            if (h > max_h) max_h = h;
        }
    }
    if (titlebar_active_) {
        int h = surf_h(titlebar_active_);
        if (h > max_h) max_h = h;
    }
    if (max_h > 0) d.title_h = max_h;

    cairo_surface_t *cs = buttons_[BTN_CLOSE].active
                              ? buttons_[BTN_CLOSE].active
                              : buttons_[BTN_CLOSE].normal;
    if (cs) d.close_w = surf_w(cs);

    cairo_surface_t *ms = buttons_[BTN_MAXIMIZE].active
                              ? buttons_[BTN_MAXIMIZE].active
                              : buttons_[BTN_MAXIMIZE].normal;
    if (ms) d.maximize_w = surf_w(ms);

    cairo_surface_t *ns = buttons_[BTN_MINIMIZE].active
                              ? buttons_[BTN_MINIMIZE].active
                              : buttons_[BTN_MINIMIZE].normal;
    if (ns) d.minimize_w = surf_w(ns);

    return d;
}

static bool is_dir(const char *path) {
    struct stat st{};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool theme::resolve_name(const char *name, char *out, const size_t out_len) {
    if (!name || !name[0]) {
        return false;
    }

    const char *config_home = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char buf[512];

    // ~/.config/steppewm/themes/<name>
    if (config_home && config_home[0]) {
        snprintf(buf, sizeof(buf), "%s/steppewm/themes/%s", config_home, name);
    } else if (home) {
        snprintf(buf, sizeof(buf), "%s/.config/steppewm/themes/%s", home, name);
    }
    if (is_dir(buf)) {
        snprintf(out, out_len, "%s", buf);
        return true;
    }

    // /usr/share/steppewm/themes/<name>
    snprintf(buf, sizeof(buf), "/usr/share/steppewm/themes/%s", name);
    if (is_dir(buf)) {
        snprintf(out, out_len, "%s", buf);
        return true;
    }

    // /usr/local/share/steppewm/themes/<name>
    snprintf(buf, sizeof(buf), "/usr/local/share/steppewm/themes/%s", name);
    if (is_dir(buf)) {
        snprintf(out, out_len, "%s", buf);
        return true;
    }

    return false;
}

void theme::lighten(const float in[4], float out[4], const float amount) {
    for (int i = 0; i < 3; i++) {
        out[i] = in[i] + (1.0f - in[i]) * amount;
    }
    out[3] = in[3];
}

void theme::darken(const float in[4], float out[4], const float amount) {
    for (int i = 0; i < 3; i++) {
        out[i] = in[i] * (1.0f - amount);
    }
    out[3] = in[3];
}

void theme::gradient_v(cairo_t *cr, const int w, const int h, const float top[4],
                       const float bottom[4]) {
    cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 0, h);
    cairo_pattern_add_color_stop_rgba(pat, 0, top[0], top[1], top[2], top[3]);
    cairo_pattern_add_color_stop_rgba(pat, 1, bottom[0], bottom[1], bottom[2], bottom[3]);
    cairo_set_source(cr, pat);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);
}

void theme::paint_titlebar(cairo_t *cr, const int w, const int h, const bool focused,
                           const char *title, const float active[4], const float inactive[4],
                           const float text_color[4],
                           const int buttons_w, const bool gradient, const char *font,
                           const int font_size, const bool center_text,
                           const bool buttons_left) const {
    const float *base = focused ? active : inactive;
    cairo_surface_t *pixmap = focused ? titlebar_active_ : titlebar_inactive_;

    if (pixmap) {
        tile_surface(cr, pixmap, w, h);
    } else if (gradient) {
        float top[4], bottom[4];
        lighten(base, top, 0.15f);
        darken(base, bottom, 0.15f);
        gradient_v(cr, w, h, top, bottom);
    } else {
        cairo_set_source_rgba(cr, base[0], base[1], base[2], base[3]);
        cairo_paint(cr);
    }

    if (text_color[3] > 0.0f && title && title[0]) {
        cairo_save(cr);
        const int text_left = buttons_left ? buttons_w : 0;
        const int text_right = buttons_left ? w : w - buttons_w;
        cairo_rectangle(cr, text_left, 0, text_right - text_left, h);
        cairo_clip(cr);

        cairo_select_font_face(cr, font && font[0] ? font : "sans-serif",
                               CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        const double fs = font_size > 0 ? font_size : h * 0.55;
        cairo_set_font_size(cr, fs);
        cairo_set_source_rgba(cr, text_color[0], text_color[1], text_color[2], text_color[3]);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, title, &ext);
        double tx;
        if (center_text) {
            tx = text_left + (text_right - text_left - ext.width) / 2.0 - ext.x_bearing;
        } else {
            tx = text_left + 8.0 - ext.x_bearing;
        }
        const double ty = h / 2.0 - ext.y_bearing - ext.height / 2.0;
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, title);
        cairo_restore(cr);
    }
}

void theme::draw_close_symbol(cairo_t *cr, const double cx, const double cy, const double size,
                              const float *color) {
    const double half = size / 2.0;
    cairo_set_source_rgba(cr, color[0], color[1], color[2], color[3]);
    cairo_set_line_width(cr, 1.5);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, cx - half, cy - half);
    cairo_line_to(cr, cx + half, cy + half);
    cairo_move_to(cr, cx + half, cy - half);
    cairo_line_to(cr, cx - half, cy + half);
    cairo_stroke(cr);
}

void theme::draw_maximize_symbol(cairo_t *cr, const double cx, const double cy, const double size,
                                 const float *color) {
    const double half = size / 2.0;
    cairo_set_source_rgba(cr, color[0], color[1], color[2], color[3]);
    cairo_set_line_width(cr, 1.5);
    cairo_rectangle(cr, cx - half, cy - half, size, size);
    cairo_stroke(cr);
}

void theme::draw_minimize_symbol(cairo_t *cr, const double cx, const double cy, const double size,
                                 const float *color) {
    const double half = size / 2.0;
    cairo_set_source_rgba(cr, color[0], color[1], color[2], color[3]);
    cairo_set_line_width(cr, 2.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, cx - half, cy);
    cairo_line_to(cr, cx + half, cy);
    cairo_stroke(cr);
}

void theme::paint_button(cairo_t *cr, const int w, const int h, const int type,
                         const bool focused, const bool hovered, const float *active,
                         const float *inactive, const float *symbol_color,
                         const char *style, const float *hover_color) const {
    const pixmap_set &ps = buttons_[type];
    cairo_surface_t *pixmap = nullptr;
    if (hovered && ps.hover) {
        pixmap = ps.hover;
    } else if (focused && ps.active) {
        pixmap = ps.active;
    } else if (ps.normal) {
        pixmap = ps.normal;
    }

    if (pixmap) {
        const int pw = cairo_image_surface_get_width(pixmap);
        const int ph = cairo_image_surface_get_height(pixmap);
        const double sx = static_cast<double>(w) / pw;
        const double sy = static_cast<double>(h) / ph;
        const double scale = sx < sy ? sx : sy;
        const double dx = (w - pw * scale) / 2.0;
        const double dy = (h - ph * scale) / 2.0;
        cairo_save(cr);
        cairo_translate(cr, dx, dy);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, pixmap, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
        return;
    }

    const double cx = w / 2.0;
    const double cy = h / 2.0;

    if (style && strcmp(style, "circle") == 0) {
        const double radius = (std::min(w, h) - 4) / 2.0;
        if (radius <= 0) return;

        const float *fill = focused ? active : inactive;
        float color[4];
        if (hovered && hover_color && hover_color[3] > 0.0f) {
            memcpy(color, hover_color, sizeof(color));
        } else if (hovered) {
            lighten(fill, color, 0.20f);
        } else {
            memcpy(color, fill, sizeof(color));
        }

        cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, color[0], color[1], color[2], color[3]);
        cairo_fill(cr);

        if (hovered) {
            const double sym_size = radius * 0.55;
            switch (type) {
                case BTN_CLOSE:
                    draw_close_symbol(cr, cx, cy, sym_size, symbol_color);
                    break;
                case BTN_MAXIMIZE:
                    draw_maximize_symbol(cr, cx, cy, sym_size, symbol_color);
                    break;
                case BTN_MINIMIZE:
                    draw_minimize_symbol(cr, cx, cy, sym_size, symbol_color);
                    break;
                default:
                    break;
            }
        }
        return;
    }

    const float *base = focused ? active : inactive;
    float color[4];
    if (hovered && hover_color && hover_color[3] > 0.0f) {
        memcpy(color, hover_color, sizeof(color));
    } else if (hovered) {
        lighten(base, color, 0.20f);
    } else {
        memcpy(color, base, sizeof(color));
    }

    cairo_set_source_rgba(cr, color[0], color[1], color[2], color[3]);
    cairo_paint(cr);

    const double sym_size = h * 0.3;

    switch (type) {
        case BTN_CLOSE:
            draw_close_symbol(cr, cx, cy, sym_size, symbol_color);
            break;
        case BTN_MAXIMIZE:
            draw_maximize_symbol(cr, cx, cy, sym_size, symbol_color);
            break;
        case BTN_MINIMIZE:
            draw_minimize_symbol(cr, cx, cy, sym_size, symbol_color);
            break;
        default:
            break;
    }
}

void theme::paint_border(cairo_t *cr, const int w, const int h, const float *base_color,
                         const int edge, const char *style) const {
    cairo_surface_t *pixmap = nullptr;
    if (edge == EDGE_LEFT) pixmap = border_left_;
    else if (edge == EDGE_RIGHT) pixmap = border_right_;
    else if (edge == EDGE_BOTTOM) pixmap = border_bottom_;

    if (pixmap) {
        const int pw = cairo_image_surface_get_width(pixmap);
        const int ph = cairo_image_surface_get_height(pixmap);
        cairo_save(cr);
        if (edge == EDGE_BOTTOM) {
            const double sy = static_cast<double>(h) / ph;
            cairo_scale(cr, 1.0, sy);
            cairo_pattern_t *pat = cairo_pattern_create_for_surface(pixmap);
            cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
            cairo_set_source(cr, pat);
            cairo_rectangle(cr, 0, 0, w, ph);
            cairo_fill(cr);
            cairo_pattern_destroy(pat);
        } else {
            const double sx = static_cast<double>(w) / pw;
            cairo_scale(cr, sx, 1.0);
            cairo_pattern_t *pat = cairo_pattern_create_for_surface(pixmap);
            cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
            cairo_set_source(cr, pat);
            cairo_rectangle(cr, 0, 0, pw, h);
            cairo_fill(cr);
            cairo_pattern_destroy(pat);
        }
        cairo_restore(cr);
        return;
    }

    cairo_set_source_rgba(cr, base_color[0], base_color[1], base_color[2], base_color[3]);
    cairo_paint(cr);

    if (style && strcmp(style, "bevel") == 0) {
        float light[4], dark[4];
        lighten(base_color, light, 0.3f);
        darken(base_color, dark, 0.3f);

        switch (edge) {
            case EDGE_LEFT:
                cairo_set_source_rgba(cr, light[0], light[1], light[2], light[3]);
                cairo_rectangle(cr, w - 1, 0, 1, h);
                cairo_fill(cr);
                break;
            case EDGE_RIGHT:
                cairo_set_source_rgba(cr, dark[0], dark[1], dark[2], dark[3]);
                cairo_rectangle(cr, w - 1, 0, 1, h);
                cairo_fill(cr);
                break;
            case EDGE_BOTTOM:
                cairo_set_source_rgba(cr, dark[0], dark[1], dark[2], dark[3]);
                cairo_rectangle(cr, 0, h - 1, w, 1);
                cairo_fill(cr);
                break;
            default: ;
        }
    }
}
