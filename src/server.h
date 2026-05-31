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

#include "config.h"

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

struct steppewm_view;
struct steppewm_layer_surface;

enum steppewm_cursor_mode {
    STEPPEWM_CURSOR_PASSTHROUGH,
    STEPPEWM_CURSOR_MOVE,
    STEPPEWM_CURSOR_RESIZE,
};

struct steppewm_server {
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_session *session;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;

    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    // outputs
    struct wlr_output_layout *output_layout;
    struct wl_list outputs;
    struct wl_listener new_output;
    struct wl_listener output_layout_change;

    // xdg shell
    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_list views;

    // layer shell
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;
    // layer surface currently holding keyboard focus
    struct steppewm_layer_surface *focused_layer;

    // decorations
    struct wlr_xdg_decoration_manager_v1 *deco_manager;
    struct wl_listener new_deco;

    // input
    struct wlr_seat *seat;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_list keyboards;

    struct wl_listener new_input;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener request_set_cursor;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;

    // move/resize grab state
    enum steppewm_cursor_mode cursor_mode;
    struct steppewm_view *grabbed_view;
    double grab_x, grab_y;
    struct wlr_box grab_geobox;
    uint32_t resize_edges;

    struct steppewm_config config;
};