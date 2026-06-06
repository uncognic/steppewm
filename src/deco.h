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

#include <stdint.h>
#include <wayland-server-core.h>

struct wlr_scene_node;

namespace steppewm {

struct server;
class view;

// event callback for new xdg decoration
void deco_new(struct wl_listener *listener, void *data);

// returns the view and sets *node to the hit rect
view* deco_at(const server* s, double lx, double ly, struct wlr_scene_node** node);

} // namespace steppewm
