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

#include <wayland-server-core.h>

struct steppewm_server;
struct steppewm_view;
struct wlr_scene_tree;
struct wlr_scene_rect;
struct wlr_scene_buffer;

#define TASKBAR_MAX 64

// button
struct steppewm_task_button {
    struct steppewm_taskbar *bar;
    struct steppewm_view *view;
    struct wlr_scene_buffer *label;
    struct wl_listener title_changed;
};

// taskbar
struct steppewm_taskbar {
    struct steppewm_server *server;
    struct wlr_scene_tree *tree;
    struct wlr_scene_rect *background;

    struct steppewm_task_button buttons[TASKBAR_MAX];
    int nbuttons;

    int x, y, width, height;
};

// see taskbar.c for more info on these methods
struct steppewm_taskbar *taskbar_create(struct steppewm_server *server);
void taskbar_destroy(struct steppewm_taskbar *bar);
void taskbar_view_added(struct steppewm_taskbar *bar, struct steppewm_view *view);
void taskbar_view_removed(struct steppewm_taskbar *bar, struct steppewm_view *view);
void taskbar_refresh(struct steppewm_taskbar *bar);
void taskbar_update_geometry(struct steppewm_taskbar *bar);
struct steppewm_view *taskbar_view_at(struct steppewm_taskbar *bar, double x, double y);
