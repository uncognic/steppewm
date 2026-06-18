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

struct wlr_scene_tree;
struct wlr_scene_buffer;
struct wl_event_source;

namespace steppewm {

class server;

class osd {
  public:
    osd(server* s);
    ~osd();
    void show(const char* text) const;

  private:
    static int on_timeout(void* data);
    server* srv_;
    wlr_scene_tree* tree_;
    wlr_scene_buffer* buf_;
    wl_event_source* timer_;
};

} // namespace steppewm
