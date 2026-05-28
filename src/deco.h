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

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

struct steppewm_view;
struct steppewm_server;
struct wlr_scene_node;


void deco_new(struct wl_listener *listener, void *data);
void deco_create(struct steppewm_view *view);
void deco_update(struct steppewm_view *view);
void deco_destroy(struct steppewm_view *view);
void deco_set_focus(struct steppewm_view *view, bool focused);
const char *deco_cursor_name(struct steppewm_view *view, struct wlr_scene_node *node);
bool deco_handle_button(struct steppewm_view *view, struct steppewm_server *server, struct wlr_scene_node *node, uint32_t button);
struct steppewm_view *deco_at(struct steppewm_server *server, double lx, double ly,
                              struct wlr_scene_node **node);
