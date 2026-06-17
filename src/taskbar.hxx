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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "config.hxx"

struct wlr_output;
struct wlr_scene_buffer;
struct wlr_scene_rect;
struct wlr_scene_tree;
struct wlr_xdg_toplevel_icon_v1;
struct wlr_xdg_toplevel_icon_v1_buffer;
struct wl_event_source;

namespace steppewm {
enum class bat_state { DISCHARGING, CHARGING, FULL, UNKNOWN };

class bat_info {
  public:
    int capacity;
    bat_state state;
    bool read_battery(const char* battery_path);
};

class task_button {
  public:
    ~task_button();
    view* v{};
    struct wlr_scene_buffer* label{};
    struct _cairo_surface* cached_icon{};
    Listener title_changed;
};

class taskbar {
  public:
    taskbar(server* s, struct wlr_output* wlr_output);
    ~taskbar();

    taskbar(const taskbar&) = delete;
    taskbar& operator=(const taskbar&) = delete;

    void view_added(view* v);
    void view_removed(view* v);
    void refresh();
    void update_geometry();
    void raise();
    void hover_update(double x, double y);
    void hover_clear();
    view* view_at(double x, double y);
    int pin_at(double x, double y) const;
    int workspace_at(double x, double y) const;
    [[nodiscard]] bool idle_inhibit_at(double x, double y) const;
    [[nodiscard]] bool brightness_at(double x, double y) const;
    [[nodiscard]] bool volume_at(double x, double y) const;
    [[nodiscard]] int tray_at(double x, double y) const;
    static void refresh_taskbars(server* s);
    static void init_monitors(server* s);
    static void fini_monitors(const server* s);

  private:
    void layout();
    static void render_button(struct wlr_scene_buffer* scene_buf, const char* text,
                              struct wlr_xdg_toplevel_icon_v1* icon,
                              struct _cairo_surface* fallback_icon, bool pinned, int w, int h,
                              float bg[4], float fg[4]);
    void render_clock();
    void render_battery();
    void render_brightness();
    void render_volume();
    void render_idle_indicator();
    void render_layout_indicator();
    void render_tray();
    void layout_code(char* out, size_t len);
    [[nodiscard]] view* focused_view() const;
    static int clock_tick(void* data);
    static int urgent_tick(void* data);
    static wlr_xdg_toplevel_icon_v1_buffer* pick_icon_buffer(struct wlr_xdg_toplevel_icon_v1* icon,
                                                             int target_size);
    server* srv_;
    struct wlr_output* wlr_output_;
    struct wlr_scene_tree* tree_;
    Listener tree_destroy_;
    struct wlr_scene_rect* background_;
    struct wlr_scene_buffer* clock_;
    struct wl_event_source* clock_timer_;
    struct wl_event_source* urgent_timer_;
    struct wlr_scene_buffer* layout_ind_;
    struct wlr_scene_buffer* battery_;
    int battery_w_ = 0;
    struct wlr_scene_buffer* brightness_;
    int brightness_w_ = 0;
    int brightness_x_ = 0;
    struct wlr_scene_buffer* volume_;
    int volume_w_ = 0;
    int volume_x_ = 0;
    struct wlr_scene_buffer* idle_ind_;
    int idle_ind_w_ = 0;
    int idle_ind_x_ = 0;
    int layout_ind_w_ = 0;
    struct wlr_scene_buffer* ws_labels_[num_workspaces]{};
    int ws_button_w_ = 0;
    class display_slot {
      public:
        view* v{};
        task_button* btn{};
        int pin_idx{-1};
        int x{};
        int w{};
    };
    std::vector<display_slot> slots_;
    std::vector<wlr_scene_buffer*> pin_labels_;
    std::vector<struct _cairo_surface*> pin_icons_;
    std::vector<std::unique_ptr<task_button>> buttons_;
    // what each tray_bufs_ slot was rendered from, to avoid rerendering what we don't need to
    class tray_buf_state {
      public:
        const void* item = nullptr;
        uint64_t gen = 0;
        int size = 0;
    };
    std::vector<wlr_scene_buffer*> tray_bufs_;
    std::vector<tray_buf_state> tray_buf_state_;
    int tray_total_w_ = 0;
    int tray_x_ = 0;
    int clock_w_ = 0;
    int button_w_ = 0;
    int x_ = 0, y_ = 0, width_ = 0, height_ = 0;
    bool urgent_flash_on_ = true;
    int hovered_slot_idx_ = -1;
    int hovered_ws_idx_ = -1;
};

} // namespace steppewm
