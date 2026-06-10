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
#include <memory>
#include <vector>

#include "config.hxx"

struct wlr_output;
struct wlr_scene_buffer;
struct wlr_scene_rect;
struct wlr_scene_tree;
struct wl_event_source;

namespace steppewm {

struct server;
class view;
struct task_button;

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
    view* view_at(double x, double y);
    int workspace_at(double x, double y);

  private:
    void layout();
    static void render_button(struct wlr_scene_buffer* scene_buf, const char* text, int w, int h,
                              float bg[4], float fg[4]);
    void render_clock();
    void render_layout_indicator();
    void layout_code(char* out, size_t len);
    [[nodiscard]] view* focused_view() const;
    static int clock_tick(void* data);
    static int urgent_tick(void* data);

    server* srv_;
    struct wlr_output* wlr_output_;
    struct wlr_scene_tree* tree_;
    struct wlr_scene_rect* background_;
    struct wlr_scene_buffer* clock_;
    struct wl_event_source* clock_timer_;
    struct wl_event_source* urgent_timer_;
    struct wlr_scene_buffer* layout_ind_;
    int layout_ind_w_ = 0;
    struct wlr_scene_buffer* ws_labels_[num_workspaces]{};
    int ws_button_w_ = 0;
    std::vector<std::unique_ptr<task_button>> buttons_;
    int clock_w_ = 0;
    int button_w_ = 0;
    int x_ = 0, y_ = 0, width_ = 0, height_ = 0;
    bool urgent_flash_on_ = true;
};

} // namespace steppewm
