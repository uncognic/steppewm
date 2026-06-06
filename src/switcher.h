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
#include <vector>

struct steppewm_server;
struct steppewm_view;
struct wlr_output;
struct wlr_scene_buffer;
struct wlr_scene_tree;

class steppewm_switcher {
  public:
    // open the overlay  and move the highlight one step
    static void cycle(struct steppewm_server* server, uint32_t mods, bool backwards);

    // focus the highlighted window once the cycle's modifiers are all released
    static void handle_modifiers(struct steppewm_server* server, uint32_t mods);

    // close the overlay without changing focus
    static void cancel(const struct steppewm_server* server);

    // drop a window that went away while the overlay is open
    static void view_removed(const struct steppewm_server* server,
                             const struct steppewm_view* view);

  private:
    steppewm_switcher(struct steppewm_server* server, std::vector<struct steppewm_view*> views,
                      uint32_t mods);
    ~steppewm_switcher();

    steppewm_switcher(const steppewm_switcher&) = delete;
    steppewm_switcher& operator=(const steppewm_switcher&) = delete;

    void advance(bool backwards);
    void render() const;
    struct wlr_output* pick_output() const;

    struct steppewm_server* server_;
    struct wlr_scene_tree* tree_;
    struct wlr_scene_buffer* panel_;

    std::vector<struct steppewm_view*> views_;
    size_t selected_ = 0;

    uint32_t mods_;
};
