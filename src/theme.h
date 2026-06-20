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

#pragma once

#include <cstddef>
#include <cairo/cairo.h>

namespace steppewm {
    class theme {
    public:
        static constexpr int BTN_CLOSE = 0;
        static constexpr int BTN_MAXIMIZE = 1;
        static constexpr int BTN_MINIMIZE = 2;

        static constexpr int EDGE_LEFT = 0;
        static constexpr int EDGE_RIGHT = 1;
        static constexpr int EDGE_BOTTOM = 2;

        theme();

        ~theme();

        theme(const theme &) = delete;

        theme &operator=(const theme &) = delete;

        struct suggested_dims {
            int title_h = 0;
            int close_w = 0;
            int maximize_w = 0;
            int minimize_w = 0;
        };

        void load(const char *theme_dir);

        static bool resolve_name(const char *name, char *out, size_t out_len);

        void unload();

        [[nodiscard]] suggested_dims get_dims() const;

        void paint_titlebar(cairo_t *cr, int w, int h, bool focused, const char *title,
                            const float active[4], const float inactive[4],
                            const float text_color[4], int buttons_w,
                            bool gradient, const char *font, int font_size,
                            bool center_text, bool buttons_left) const;

        void paint_button(cairo_t *cr, int w, int h, int type, bool focused, bool hovered,
                          const float *active, const float *inactive,
                          const float *symbol_color, const char *style,
                          const float *hover_color) const;

        void paint_border(cairo_t *cr, int w, int h, const float *base_color, int edge,
                          const char *style) const;

        void paint_taskbar_bg(cairo_t *cr, int w, int h, const float *fallback,
                              const float *accent) const;

        void paint_task_button(cairo_t *cr, int w, int h, bool active, bool minimized,
                               const float *fallback) const;

        void paint_workspace_button(cairo_t *cr, int w, int h, bool active,
                                    const float *fallback) const;

        void paint_tray_bg(cairo_t *cr, int w, int h, const float *fallback) const;

        void paint_indicator_bg(cairo_t *cr, int w, int h, const float *fallback) const;

        static void lighten(const float in[4], float out[4], float amount);

        static void darken(const float in[4], float out[4], float amount);

        static void gradient_v(cairo_t *cr, int w, int h, const float top[4], const float bottom[4]);

    private:
        struct pixmap_set {
            cairo_surface_t *normal = nullptr;
            cairo_surface_t *active = nullptr;
            cairo_surface_t *hover = nullptr;
        };

        pixmap_set buttons_[3];
        cairo_surface_t *titlebar_active_ = nullptr;
        cairo_surface_t *titlebar_inactive_ = nullptr;

        cairo_surface_t *border_left_ = nullptr;
        cairo_surface_t *border_right_ = nullptr;
        cairo_surface_t *border_bottom_ = nullptr;

        cairo_surface_t *taskbar_bg_ = nullptr;
        cairo_surface_t *taskbutton_ = nullptr;
        cairo_surface_t *taskbutton_active_ = nullptr;
        cairo_surface_t *taskbutton_minimized_ = nullptr;
        cairo_surface_t *workspace_button_ = nullptr;
        cairo_surface_t *workspace_button_active_ = nullptr;
        cairo_surface_t *tray_bg_pm_ = nullptr;
        cairo_surface_t *indicator_bg_ = nullptr;

        static cairo_surface_t *try_load(const char *dir, const char *stem);

        static void tile_surface(cairo_t *cr, cairo_surface_t *surf, int w, int h);

        static void draw_close_symbol(cairo_t *cr, double cx, double cy, double size,
                                      const float *color);

        static void draw_maximize_symbol(cairo_t *cr, double cx, double cy, double size,
                                         const float *color);

        static void draw_minimize_symbol(cairo_t *cr, double cx, double cy, double size,
                                         const float *color);
    };
} // namespace steppewm
