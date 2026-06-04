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

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct steppewm_server;
struct steppewm_view;
struct wlr_output;
struct wlr_scene_tree;
struct wlr_scene_rect;
struct wlr_scene_buffer;
struct wl_event_source;

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
    struct wlr_output *wlr_output;
    struct wlr_scene_tree *tree;
    struct wlr_scene_rect *background;

    struct wlr_scene_buffer *clock;
    struct wl_event_source *clock_timer;

    struct wlr_scene_buffer *ws_labels[STEPPEWM_NUM_WORKSPACES];
    int ws_button_w;

    struct steppewm_task_button buttons[TASKBAR_MAX];
    int nbuttons;

    int clock_w;  // measured width of the clock label
    int button_w; // dynamically adjusting button width

    int x, y, width, height;
};

// see taskbar.c for more info on these methods
struct steppewm_taskbar *taskbar_create(struct steppewm_server *server,
                                        struct wlr_output *wlr_output);
void taskbar_destroy(struct steppewm_taskbar *bar);
void taskbar_view_added(struct steppewm_taskbar *bar, struct steppewm_view *view);
void taskbar_view_removed(struct steppewm_taskbar *bar, struct steppewm_view *view);
void taskbar_refresh(struct steppewm_taskbar *bar);
void taskbar_update_geometry(struct steppewm_taskbar *bar);
struct steppewm_view *taskbar_view_at(struct steppewm_taskbar *bar, double x, double y);
// return the workspace index under (x, y), or -1 if no indicator button is there
int taskbar_workspace_at(struct steppewm_taskbar *bar, double x, double y);

#ifdef __cplusplus
}
#endif
