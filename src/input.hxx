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

#include "listener.hxx"
#include "server.hxx"

#include <wayland-server-core.h>

struct wlr_keyboard;
struct wlr_input_device;
struct wlr_pointer_constraint_v1;
struct wlr_idle_inhibitor_v1;

namespace steppewm {

class view;

struct keyboard {
    server* srv;
    wlr_keyboard* wlr_keyboard;

    wl_list link;

    wl_listener modifiers;
    wl_listener key;
    wl_listener destroy;
};

struct pointer {
    server* srv;
    wlr_input_device* device;

    wl_list link;

    wl_listener destroy;
};

class pointer_constraint {
  public:
    pointer_constraint(server* s, wlr_pointer_constraint_v1* constraint);

    static void on_new(wl_listener* listener, void* data);
    static void update(server* s);

  private:
    void handle_destroy() const;

    server* srv;
    wlr_pointer_constraint_v1* constraint;
    Listener destroy;
};

class idle_inhibitor {
  public:
    idle_inhibitor(server* s, wlr_idle_inhibitor_v1* inhibitor);

    static void on_new(wl_listener* listener, void* data);
    static void update(server* s, const wlr_idle_inhibitor_v1* exclude = nullptr);

  private:
    server* srv;
    wlr_idle_inhibitor_v1* inhibitor;
    Listener destroy;
};

void input_new(struct wl_listener* listener, void* data);
void input_reconfigure(server* s);

void cursor_motion(struct wl_listener* listener, void* data);
void cursor_motion_absolute(struct wl_listener* listener, void* data);
void cursor_button(struct wl_listener* listener, void* data);
void cursor_axis(struct wl_listener* listener, void* data);
void cursor_frame(struct wl_listener* listener, void* data);

void request_set_cursor(struct wl_listener* listener, void* data);
void request_set_shape(struct wl_listener* listener, void* data);
void request_set_selection(struct wl_listener* listener, void* data);
void request_set_primary_selection(struct wl_listener* listener, void* data);

void cursor_begin_interactive(view* v, cursor_mode mode, uint32_t edges);

} // namespace steppewm
