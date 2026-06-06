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

#include "server.h"

#include <wayland-server-core.h>

struct wlr_keyboard;
struct wlr_input_device;

namespace steppewm {

class view;

struct keyboard {
    server* srv;
    struct wlr_keyboard *wlr_keyboard;

    struct wl_list link;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

struct pointer {
    server* srv;
    struct wlr_input_device* device;

    struct wl_list link;

    struct wl_listener destroy;
};

void input_new(struct wl_listener *listener, void *data);
void input_reconfigure(server* s);

void cursor_motion(struct wl_listener *listener, void *data);
void cursor_motion_absolute(struct wl_listener *listener, void *data);
void cursor_button(struct wl_listener *listener, void *data);
void cursor_axis(struct wl_listener *listener, void *data);
void cursor_frame(struct wl_listener *listener, void *data);

void request_set_cursor(struct wl_listener *listener, void *data);
void request_set_selection(struct wl_listener *listener, void *data);
void request_set_primary_selection(struct wl_listener *listener, void *data);

void cursor_begin_interactive(view* v, cursor_mode mode, uint32_t edges);

} // namespace steppewm
