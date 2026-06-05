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
#include <utility>

extern "C" {
#include <wayland-server-core.h>
}

namespace steppe {

// raii wrapper around wl listeners
class Listener {
  public:
    Listener() {
        node_.self = this;
        node_.listener.notify = nullptr;
        wl_list_init(&node_.listener.link);
        handler_ = nullptr;
    }
    ~Listener() { disconnect(); }

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    void connect(struct wl_signal* signal, std::function<void(void*)> handler) {
        disconnect();
        handler_ = new std::function<void(void*)>(std::move(handler));
        node_.listener.notify = on_notify;
        wl_signal_add(signal, &node_.listener);
    }

    void disconnect() {
        if (connected()) {
            wl_list_remove(&node_.listener.link);
        }
        wl_list_init(&node_.listener.link);
        node_.listener.notify = nullptr;
        delete handler_;
        handler_ = nullptr;
    }

    [[nodiscard]] bool connected() const {
        return node_.listener.notify != nullptr && node_.listener.link.next != &node_.listener.link;
    }

  private:
    struct Node {
        struct wl_listener listener;
        Listener* self;
    };

    static void on_notify(struct wl_listener* listener, void* data) {
        Node* node = wl_container_of(listener, node, listener);
        if (node->self->handler_) {
            (*node->self->handler_)(data);
        }
    }

    Node node_;
    std::function<void(void*)>* handler_;
};

} // namespace steppe
