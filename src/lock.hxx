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

struct wlr_session_lock_v1;
struct wlr_session_lock_surface_v1;

namespace steppewm {

// one active ext-session-lock-v1 lock
class session_lock {
  public:
    session_lock(server* s, wlr_session_lock_v1* lock);

    static void init(server* s);
    static void on_new(wl_listener* listener, void* data);
    static void update_geometry(server* s);
    static void ensure_focus(server* s);

  private:
    server* srv;
    Listener new_surface;
    Listener unlock;
    Listener destroy;
};

class lock_surface {
  public:
    lock_surface(server* s, struct wlr_session_lock_surface_v1* lock_surface);
    void configure() const;

  private:
    void handle_destroy() const;

    server* srv;
    wlr_session_lock_surface_v1* wlr_lock_surface;
    wlr_scene_tree* tree;
    Listener destroy;
};

} // namespace steppewm
