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

#ifdef HAVE_SDBUS

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct wl_event_source;

namespace sdbus {
class IConnection;
class IObject;
class IProxy;
} // namespace sdbus

namespace steppewm {

class server;

struct tray_item {
    std::string bus_name;
    std::string object_path;
    std::unique_ptr<sdbus::IProxy> proxy;
    std::vector<uint32_t> icon_pixels;
    int icon_width = 0;
    int icon_height = 0;
    uint64_t icon_gen = 0;
};

// org.kde.StatusNotifierWatcher
class tray_host {
  public:
    static tray_host* create(server* s);
    ~tray_host();

    tray_host(const tray_host&) = delete;
    tray_host& operator=(const tray_host&) = delete;

    [[nodiscard]] const std::vector<std::unique_ptr<tray_item>>& items() const { return items_; }
    void activate(int index, int x, int y) const;
    void secondary_activate(int index, int x, int y) const;
    void context_menu(int index, int x, int y) const;

  private:
    tray_host() = default;
    void handle_register(const std::string& service, const std::string& sender);
    void handle_unregister(const std::string& bus_name);
    void fetch_icon(tray_item* item);
    void fetch_icon_name(tray_item* item) const;
    void refresh_all() const;
    static int on_dbus_event(int fd, uint32_t mask, void* data);
    static int on_dbus_timer(void* data);
    void update_event_source() const;

    server* srv_ = nullptr;
    std::unique_ptr<sdbus::IConnection> conn_;
    std::unique_ptr<sdbus::IObject> watcher_obj_;
    std::unique_ptr<sdbus::IProxy> dbus_proxy_;
    wl_event_source* dbus_source_ = nullptr;
    wl_event_source* dbus_event_source_ = nullptr;
    wl_event_source* dbus_timer_ = nullptr;
    std::vector<std::unique_ptr<tray_item>> items_;
};

} // namespace steppewm

#endif
