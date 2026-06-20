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

#include <cairo/cairo.h>

struct wlr_scene_buffer;
struct cpu_buf;

namespace paint {

class Canvas {
  public:
    Canvas(int width, int height);
    ~Canvas();

    Canvas(const Canvas &) = delete;
    Canvas &operator=(const Canvas &) = delete;

    [[nodiscard]] bool valid() const { return cr_ != nullptr; }
    [[nodiscard]] cairo_t *cr() const { return cr_; }
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

    void commit(struct wlr_scene_buffer *scene_buf);

  private:
    int width_ = 0;
    int height_ = 0;
    cpu_buf *buf_ = nullptr;
    cairo_surface_t *surface_ = nullptr;
    cairo_t *cr_ = nullptr;
};

cairo_text_extents_t text_extents(const char *text, double font_size,
                                  const char *font = "sans-serif");
} // namespace paint
