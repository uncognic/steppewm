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
#include <wayland-server-core.h>

struct parwm_view;

#define PARWM_TITLE_H 26
#define PARWM_BORDER_W 1

void deco_new(struct wl_listener *listener, void *data);
void deco_create(struct parwm_view *view);
void deco_update(struct parwm_view *view);
void deco_destroy(struct parwm_view *view);
void deco_set_focus(struct parwm_view *view, bool focused);
