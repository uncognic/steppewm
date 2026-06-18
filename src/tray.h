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
struct wlr_scene_tree;
struct wlr_scene_buffer;
struct wlr_scene_rect;

namespace sdbus {
class IConnection;
class IObject;
class IProxy;
} // namespace sdbus

namespace steppewm {

class server;

struct menu_entry {
    int32_t id = 0;
    std::string label;
    bool enabled = true;
    bool visible = true;
    bool separator = false;
    std::string toggle_type;
    int32_t toggle_state = -1;
};

struct tray_item {
    std::string bus_name;
    std::string object_path;
    std::string menu_path;
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
    void context_menu(int index, int x, int y);

    [[nodiscard]] bool menu_open() const { return menu_tree_ != nullptr; }
    void close_menu();
    int menu_item_at(double x, double y) const;
    void menu_click(int item_index);
    void menu_hover(double x, double y);

  private:
    tray_host() = default;
    void handle_register(const std::string& service, const std::string& sender);
    void handle_unregister(const std::string& bus_name);
    void fetch_icon(tray_item* item);
    void fetch_icon_name(tray_item* item) const;
    static void fetch_menu_path(tray_item* item);
    void refresh_all() const;
    void render_menu();
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

    tray_item* menu_item_ = nullptr;
    std::unique_ptr<sdbus::IProxy> menu_proxy_;
    std::vector<menu_entry> menu_entries_;
    wlr_scene_tree* menu_tree_ = nullptr;
    wlr_scene_buffer* menu_buf_ = nullptr;
    int menu_x_ = 0, menu_y_ = 0;
    int menu_w_ = 0, menu_h_ = 0;
    int menu_item_h_ = 0;
    int menu_hovered_ = -1;
};

} // namespace steppewm

#endif
