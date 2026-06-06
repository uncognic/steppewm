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

#include <functional>

extern "C" {
#include <wayland-server-core.h>
}

// raii wrapper around wl listeners
class Listener {
  public:
    Listener();
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    void connect(struct wl_signal* signal, std::function<void(void*)> handler);

    void disconnect();

    [[nodiscard]] bool connected() const;

  private:
    struct Node {
        struct wl_listener listener;
        Listener* self;
    };

    static void on_notify(struct wl_listener* listener, void* data);

    Node node_{};
    std::function<void(void*)>* handler_;
};
