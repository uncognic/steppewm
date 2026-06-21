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

#include "listener.h"
#include "server.h"

struct wlr_input_device;
struct wlr_tablet;
struct wlr_tablet_tool;
struct wlr_tablet_pad;
struct wlr_tablet_v2_tablet;
struct wlr_tablet_v2_tablet_tool;
struct wlr_tablet_v2_tablet_pad;

namespace steppewm {
  // the stylus/pen
  class tablet_tool {
  public:
    server *srv{};
    wlr_tablet *tablet{};
    wlr_tablet_tool *wlr_tool{};
    wl_list link{};

    Listener axis;
    Listener proximity;
    Listener tip;
    Listener button;
    Listener device_destroy;

    wlr_tablet_v2_tablet *tablet_v2{};
    wlr_tablet_v2_tablet_tool *tool_v2{};

    static void create(server *s, wlr_input_device *device);

  private:
    tablet_tool() = default;

    static void map_to_layout(const server *s, double tx, double ty, double *lx, double *ly);

    static wlr_surface *surface_at(const server *s, double lx, double ly, double *sx, double *sy);

    void handle_axis(void *data) const;

    void handle_proximity(void *data);

    void handle_tip(void *data) const;

    void handle_button(void *data) const;

    void destroy();
  };

  // the tablet itself
  class tablet_pad {
  public:
    server *srv{};
    wlr_tablet_pad *pad{};

    wl_list link{};

    Listener button;
    Listener ring;
    Listener strip;
    Listener device_destroy;

    wlr_tablet_v2_tablet_pad *pad_v2{};

    static void create(server *s, wlr_input_device *device);

  private:
    tablet_pad() = default;

    void handle_button(void *data) const;

    void handle_ring(void *data) const;

    void handle_strip(void *data) const;

    void destroy();
  };
} // namespace steppewm
