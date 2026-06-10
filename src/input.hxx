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
#include "view.hxx"

#include <wayland-server-core.h>

struct wlr_keyboard;
struct wlr_input_device;
struct wlr_pointer_constraint_v1;
struct wlr_idle_inhibitor_v1;

namespace steppewm {

class view;

class keyboard {
  public:
    server* srv;
    wlr_keyboard* wlr_keyboard;

    wl_list link;

    Listener modifiers;
    Listener key;
    Listener destroy;

    static void create(server* s, struct wlr_input_device* device);
    static void apply_config(server* s, struct wlr_keyboard* wlr_keyboard);

  private:
    static void handle_modifiers(keyboard* kbd);
    static void handle_key(keyboard* kbd, void* data);
    static void handle_destroy(keyboard* kbd);
};

class pointer {
  public:
    server* srv;
    wlr_input_device* device;

    wl_list link;

    Listener destroy;

    static void create(server* s, struct wlr_input_device* device);
    static void apply_config(server* s, struct wlr_input_device* device);

  private:
    static void handle_destroy(pointer* ptr);
};

class pointer_constraint {
  public:
    pointer_constraint(server* s, wlr_pointer_constraint_v1* constraint);

    static void init(server* s);
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

    static void init(server* s);
    static void on_new(wl_listener* listener, void* data);
    static void update(server* s, const wlr_idle_inhibitor_v1* exclude = nullptr);

  private:
    static bool visible(server* s, struct wlr_surface* surface);

    server* srv;
    wlr_idle_inhibitor_v1* inhibitor;
    Listener destroy;
};

class snap_detect {
  public:
    snap_edge edge;
    bool maximize;
    wlr_box zone;
};

} // namespace steppewm
