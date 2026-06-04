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

#include <cstdint>
#include <cstdlib>

#include <drm_fourcc.h>

#include "paint.hpp"

// cpu backed wlf buffer
// cairo draws into this and the scene graph reads it
struct cpu_buf {
    struct wlr_buffer base;
    uint8_t *pixels;
    size_t stride;
};

namespace {

void cpu_buf_destroy(struct wlr_buffer *wlr_buf) {
    cpu_buf *buf = wl_container_of(wlr_buf, buf, base);
    free(buf->pixels);
    free(buf);
}

bool cpu_buf_begin_data_ptr_access(struct wlr_buffer *wlr_buf, uint32_t flags, void **data,
                                   uint32_t *format, size_t *stride) {
    (void) flags;
    cpu_buf *buf = wl_container_of(wlr_buf, buf, base);
    *data = buf->pixels;
    *format = DRM_FORMAT_ARGB8888;
    *stride = buf->stride;
    return true;
}

void cpu_buf_end_data_ptr_access(struct wlr_buffer *wlr_buf) {
    (void) wlr_buf;
}

const struct wlr_buffer_impl cpu_buf_impl = {
    .destroy = cpu_buf_destroy,
    .get_dmabuf = nullptr,
    .get_shm = nullptr,
    .begin_data_ptr_access = cpu_buf_begin_data_ptr_access,
    .end_data_ptr_access = cpu_buf_end_data_ptr_access,
};

cpu_buf *cpu_buf_create(int w, int h) {
    cpu_buf *buf = static_cast<cpu_buf *>(calloc(1, sizeof(*buf)));
    buf->stride = (size_t) w * 4;
    buf->pixels = static_cast<uint8_t *>(calloc(h, buf->stride));
    wlr_buffer_init(&buf->base, &cpu_buf_impl, w, h);
    return buf;
}

} // namespace

namespace paint {

Canvas::Canvas(int width, int height) : width_(width), height_(height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    buf_ = cpu_buf_create(width, height);
    surface_ = cairo_image_surface_create_for_data(buf_->pixels, CAIRO_FORMAT_ARGB32, width, height,
                                                   (int) buf_->stride);
    cr_ = cairo_create(surface_);
}

Canvas::~Canvas() {
    if (cr_) {
        cairo_destroy(cr_);
    }
    if (surface_) {
        cairo_surface_destroy(surface_);
    }
    if (buf_) {
        wlr_buffer_drop(&buf_->base);
    }
}

void Canvas::commit(struct wlr_scene_buffer *scene_buf) {
    if (!cr_) {
        return;
    }

    cairo_destroy(cr_);
    cr_ = nullptr;
    cairo_surface_destroy(surface_);
    surface_ = nullptr;

    wlr_scene_buffer_set_buffer(scene_buf, &buf_->base);
    wlr_buffer_drop(&buf_->base); // scene holds its own ref now
    buf_ = nullptr;
}

cairo_text_extents_t text_extents(const char *text, double font_size) {
    cairo_surface_t *measure = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(measure);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);

    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);

    cairo_destroy(cr);
    cairo_surface_destroy(measure);
    return ext;
}

} // namespace paint
