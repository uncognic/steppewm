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

struct wlr_output;
struct wlr_scene_buffer;
struct wlr_scene_tree;

namespace steppewm {

class server;
class view;

class switcher {
  public:
    static void cycle(server* s, uint32_t mods, bool backwards);
    static void handle_modifiers(server* s, uint32_t mods);
    static void cancel(const server* s);
    static void view_removed(const server* s, const view* v);

  private:
    switcher(server* s, std::vector<view*> views, uint32_t mods);
    ~switcher();

    switcher(const switcher&) = delete;
    switcher& operator=(const switcher&) = delete;

    void advance(bool backwards);
    void render() const;
    struct wlr_output* pick_output() const;

    server* server_;
    struct wlr_scene_tree* tree_;
    struct wlr_scene_buffer* panel_;

    std::vector<view*> views_;
    size_t selected_ = 0;

    uint32_t mods_;
};

} // namespace steppewm
