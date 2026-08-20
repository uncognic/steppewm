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

#include "wlr.h" // must be first

#include "input.h"
#include "osd.h"
#include "output.h"
#include "server.h"
#include "switcher.h"
#include "taskbar.h"
#include "view.h"

using namespace steppewm;

bool view::can_configure(const view* v) {
    return v->toplevel->base->initialized;
}

// show or hide a view depending on whether it lives on the current workspace
void view::update_visibility() const {
    const bool visible = !minimized && (pinned || workspace == srv->current_workspace);
    wlr_scene_node_set_enabled(&scene_tree->node, visible);

    // hidden windows shouldn't keep blocking idle
    idle_inhibitor::update(srv);
}

void view::focus_next(server* s, const view* skip) {
    if (s->locked) {
        return;
    }

    view* next = nullptr;
    view* v;

    // loop through views until we find one on this workspace that isn't minimzed
    wl_list_for_each(v, &s->views, link) {
        if (v != skip && v->mapped && !v->minimized &&
            (v->pinned || v->workspace == s->current_workspace)) {
            next = v;
            break;
        }
    }

    // focus the next window, otherwise clear focus
    if (next) {
        next->focus(next->toplevel->base->surface);
    } else {
        wlr_seat_keyboard_notify_clear_focus(s->seat);
        taskbar::refresh_taskbars(s);
    }
}

// minimize a view and hide it from the scene
void view::minimize(bool min) {
    minimized = min;
    update_visibility();
    if (foreign_handle) {
        wlr_foreign_toplevel_handle_v1_set_minimized(foreign_handle, min);
    }
    taskbar::refresh_taskbars(srv);
}

void view::set_urgent(bool is_urgent) {
    if (urgent == is_urgent) {
        return;
    }
    urgent = is_urgent;
    taskbar::refresh_taskbars(srv);
}

// switch the visible workspace, hiding the old set and showing the new one
void view::workspace_switch(server* s, int workspace) {
    if (workspace < 0 || workspace >= num_workspaces || workspace == s->current_workspace) {
        return;
    }
    s->current_workspace = workspace;
    if (workspace > s->max_visible_workspace) {
        s->max_visible_workspace = workspace;
    }

    switcher::cancel(s);

    // toggle visibility of every window for the new workspace
    view* v;
    wl_list_for_each(v, &s->views, link) {
        v->update_visibility();
    }

    // focus the topmost window on the new workspace, otherwise clear focus
    view* focus = nullptr;
    wl_list_for_each(v, &s->views, link) {
        if (v->mapped && !v->minimized && (v->pinned || v->workspace == workspace)) {
            focus = v;
            break;
        }
    }
    if (focus) {
        focus->focus(focus->toplevel->base->surface);
    } else {
        wlr_seat_keyboard_notify_clear_focus(s->seat);
    }

    // redraw taskbars so the active workspace and window list update
    taskbar::refresh_taskbars(s);

    if (s->osd_overlay) {
        char text[32];
        snprintf(text, sizeof(text), "Workspace %d", workspace + 1);
        s->osd_overlay->show(text);
    }
}

// move a window to another workspace
void view::move_to_workspace(int ws) {
    if (ws < 0 || ws >= num_workspaces || (!pinned && workspace == ws)) {
        return;
    }
    pinned = false;
    workspace = ws;
    if (ws > srv->max_visible_workspace) {
        srv->max_visible_workspace = ws;
    }
    update_visibility();

    // the window left the current workspace
    switcher::view_removed(srv, this);

    // the window left the current workspace, hand focus to a remaining one
    view::focus_next(srv, this);
    taskbar::refresh_taskbars(srv);
}

static uint32_t snap_tiled_edges(const snap_edge edge) {
    switch (edge) {
        case snap_edge::LEFT:
            return WLR_EDGE_LEFT | WLR_EDGE_TOP | WLR_EDGE_BOTTOM;
        case snap_edge::RIGHT:
            return WLR_EDGE_RIGHT | WLR_EDGE_TOP | WLR_EDGE_BOTTOM;
        case snap_edge::TOP_LEFT:
            return WLR_EDGE_LEFT | WLR_EDGE_TOP;
        case snap_edge::TOP_RIGHT:
            return WLR_EDGE_RIGHT | WLR_EDGE_TOP;
        case snap_edge::BOTTOM_LEFT:
            return WLR_EDGE_LEFT | WLR_EDGE_BOTTOM;
        case snap_edge::BOTTOM_RIGHT:
            return WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM;
        default:
            return 0;
    }
}

void view::snap_to(const snap_edge edge) {
    if (!can_configure(this)) {
        return;
    }

    server* s = srv;
    const bool was_special = maximized || fullscreen || snapped != snap_edge::NONE;

    if (edge == snap_edge::NONE) {
        if (!was_special) {
            return;
        }
        const int ox = decoration_mode == deco_mode::SERVER ? s->cfg.border_w : 0;
        const int oy = decoration_mode == deco_mode::SERVER ? s->cfg.title_h : 0;
        wlr_scene_node_set_position(&xdg_tree->node, ox, oy);
        wlr_scene_node_set_position(&scene_tree->node, saved_geo.x, saved_geo.y);
        wlr_xdg_toplevel_set_size(toplevel, saved_geo.width, saved_geo.height);
        maximized = false;
        fullscreen = false;
        snapped = snap_edge::NONE;
        wlr_xdg_toplevel_set_maximized(toplevel, false);
        wlr_xdg_toplevel_set_fullscreen(toplevel, false);
        wlr_xdg_toplevel_set_tiled(toplevel, 0);
        deco_set_visible(true);
        raise_overlays(s);
        wlr_xdg_surface_schedule_configure(toplevel->base);
        return;
    }

    if (!was_special) {
        const wlr_box* geo = &toplevel->base->geometry;
        saved_geo = (wlr_box) {
            .x = scene_tree->node.x,
            .y = scene_tree->node.y,
            .width = geo->width,
            .height = geo->height,
        };
    }

    wlr_output* wlr_out = wlr_output_layout_output_at(s->output_layout, s->cursor->x, s->cursor->y);
    if (!wlr_out) {
        wlr_out =
            wlr_output_layout_output_at(s->output_layout, scene_tree->node.x, scene_tree->node.y);
    }
    if (!wlr_out) {
        return;
    }

    const wlr_box out = output::usable_area(s, wlr_out);

    const auto ox = decoration_mode == deco_mode::SERVER ? s->cfg.border_w : 0;
    const auto oy = decoration_mode == deco_mode::SERVER ? s->cfg.title_h : 0;
    const auto usable_h = out.height;
    const auto half_w = out.width / 2;
    const auto half_h = usable_h / 2;

    int sx, sy, sw, sh;
    switch (edge) {
        case snap_edge::LEFT:
            sx = out.x;
            sy = out.y;
            sw = half_w;
            sh = usable_h;
            break;
        case snap_edge::RIGHT:
            sx = out.x + half_w;
            sy = out.y;
            sw = out.width - half_w;
            sh = usable_h;
            break;
        case snap_edge::TOP_LEFT:
            sx = out.x;
            sy = out.y;
            sw = half_w;
            sh = half_h;
            break;
        case snap_edge::TOP_RIGHT:
            sx = out.x + half_w;
            sy = out.y;
            sw = out.width - half_w;
            sh = half_h;
            break;
        case snap_edge::BOTTOM_LEFT:
            sx = out.x;
            sy = out.y + half_h;
            sw = half_w;
            sh = usable_h - half_h;
            break;
        case snap_edge::BOTTOM_RIGHT:
            sx = out.x + half_w;
            sy = out.y + half_h;
            sw = out.width - half_w;
            sh = usable_h - half_h;
            break;
        default:
            return;
    }

    wlr_scene_node_set_position(&scene_tree->node, sx, sy);
    wlr_scene_node_set_position(&xdg_tree->node, ox, oy);
    wlr_xdg_toplevel_set_size(toplevel, sw - 2 * ox, sh - oy - ox);

    maximized = false;
    fullscreen = false;
    snapped = edge;
    wlr_xdg_toplevel_set_maximized(toplevel, false);
    wlr_xdg_toplevel_set_fullscreen(toplevel, false);
    wlr_xdg_toplevel_set_tiled(toplevel, snap_tiled_edges(edge));
    deco_set_visible(true);
    raise_overlays(s);
    wlr_xdg_surface_schedule_configure(toplevel->base);
}

// apply ssd deco
void view::apply_pending_deco(view* v) {
    if (!v->pending_deco || !can_configure(v)) {
        return;
    }

    wlr_xdg_toplevel_decoration_v1_set_mode(v->pending_deco,
                                            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    v->decoration_mode = deco_mode::SERVER;
    wlr_scene_node_set_position(&v->xdg_tree->node, v->srv->cfg.border_w, v->srv->cfg.title_h);
    v->deco_create();
    v->pending_deco = nullptr;
}

// move
void view::initial_configure(void* data) {
    view* v = static_cast<view*>(data);
    v->initial_configure_idle = nullptr;

    if (!can_configure(v)) {
        return;
    }

    apply_pending_deco(v);
    if (v->toplevel->requested.maximized || v->toplevel->requested.fullscreen) {
        apply_state(v, v->toplevel->requested.maximized, v->toplevel->requested.fullscreen);
    } else {
        wlr_xdg_toplevel_set_size(v->toplevel, 0, 0);
    }
}

// full decorated bounding box of a view at its current scene position
void view::get_box(view* v, struct wlr_box* box) {
    struct wlr_box* geo = &v->toplevel->base->geometry;
    int bw = v->decoration_mode == deco_mode::SERVER ? v->srv->cfg.border_w : 0;
    int th = v->decoration_mode == deco_mode::SERVER ? v->srv->cfg.title_h : 0;
    box->x = v->scene_tree->node.x;
    box->y = v->scene_tree->node.y;
    box->width = geo->width + 2 * bw;
    box->height = geo->height + th + bw;
}

// cascadeing
// each new window steps down right from the last
// when it gets to the bottom we go back to the top but sdhifted to the right
void view::place(view* v) {
    server* s = v->srv;

    // already-positioned states manage their own geometry
    if (v->maximized || v->fullscreen) {
        return;
    }

    // choose the output under the cursor, falling back to the first output
    struct wlr_output* wlr_out =
        wlr_output_layout_output_at(s->output_layout, s->cursor->x, s->cursor->y);
    if (!wlr_out) {
        output* o;
        wl_list_for_each(o, &s->outputs, link) {
            wlr_out = o->wlr_output;
            break;
        }
    }
    if (!wlr_out) {
        return;
    }

    // output box minus the taskbar, wherever the taskbar is
    struct wlr_box area = output::usable_area(s, wlr_out);
    if (area.height < 0) {
        area.height = 0;
    }

    // size of the window being placed with decorations
    struct wlr_box self;
    get_box(v, &self);
    int w = self.width;
    int h = self.height;

    // diagonal step
    // by default 23 px
    int step = s->cfg.title_h + s->cfg.border_w;

    // safety check
    if (step <= 0) {
        step = 30;
    }

    // rightward shift when starting a new column
    int col_step = 2 * step;

    int max_off_x = area.width - w;
    int max_off_y = area.height - h;
    if (max_off_x < 0) {
        max_off_x = 0;
    }
    if (max_off_y < 0) {
        max_off_y = 0;
    }

    // vertical position within the current column
    int y_off = s->cascade_n * step;
    if (y_off > max_off_y) {
        // start a new column, offset to the right when hit to the bottom
        s->cascade_n = 0;
        y_off = 0;
        s->cascade_x += col_step;
    }
    // columns filled the width, wrap back to the left edge
    if (s->cascade_x > max_off_x) {
        s->cascade_x = 0;
    }

    // diagonal offset down the column
    int x_off = s->cascade_x + s->cascade_n * step;
    if (x_off > max_off_x) {
        x_off = max_off_x;
    }
    s->cascade_n++;

    wlr_scene_node_set_position(&v->scene_tree->node, area.x + x_off, area.y + y_off);
}

// focus a new window
void view::handle_map(view* v) {
    // new windows open on the currently visible workspace
    v->mapped = true;
    v->workspace = v->srv->current_workspace;
    wl_list_insert(&v->srv->views, &v->link);
    v->update_visibility();

    // position the window to avoid overlap
    place(v);

    // add window to all taskbars
    output* out;
    // update for each output's taskbar
    wl_list_for_each(out, &v->srv->outputs, link) {
        if (out->output_taskbar) {
            out->output_taskbar->view_added(v);
        }
    }
    // foreign-toplevel management handle
    v->foreign_handle = wlr_foreign_toplevel_handle_v1_create(v->srv->foreign_toplevel_mgr);
    if (v->foreign_handle) {
        wlr_foreign_toplevel_handle_v1_set_title(v->foreign_handle, v->toplevel->title);
        wlr_foreign_toplevel_handle_v1_set_app_id(v->foreign_handle, v->toplevel->app_id);

        v->ft_request_maximize.connect(
            &v->foreign_handle->events.request_maximize, [v](void* data) {
                auto* e = static_cast<wlr_foreign_toplevel_handle_v1_maximized_event*>(data);
                apply_state(v, e->maximized, v->fullscreen);
            });
        v->ft_request_minimize.connect(
            &v->foreign_handle->events.request_minimize, [v](void* data) {
                auto* e = static_cast<wlr_foreign_toplevel_handle_v1_minimized_event*>(data);
                if (e->minimized) {
                    v->minimize(true);
                    view::focus_next(v->srv, v);
                } else {
                    v->minimize(false);
                    v->focus(v->toplevel->base->surface);
                }
            });
        v->ft_request_activate.connect(&v->foreign_handle->events.request_activate, [v](void*) {
            if (v->workspace != v->srv->current_workspace) {
                workspace_switch(v->srv, v->workspace);
            }
            v->focus(v->toplevel->base->surface);
        });
        v->ft_request_fullscreen.connect(
            &v->foreign_handle->events.request_fullscreen, [v](void* data) {
                auto* e = static_cast<wlr_foreign_toplevel_handle_v1_fullscreen_event*>(data);
                apply_state(v, v->maximized, e->fullscreen);
            });
        v->ft_request_close.connect(&v->foreign_handle->events.request_close,
                                    [v](void*) { wlr_xdg_toplevel_send_close(v->toplevel); });
    }

    // ext-foreign-toplevel-list handle
    const wlr_ext_foreign_toplevel_handle_v1_state ext = {
        .title = v->toplevel->title,
        .app_id = v->toplevel->app_id,
    };
    v->foreign_ext_handle =
        wlr_ext_foreign_toplevel_handle_v1_create(v->srv->foreign_toplevel_list, &ext);

    v->focus(v->toplevel->base->surface);
}

// remove window (view)
void view::handle_unmap(view* v) {
    if (v->srv->grabbed_view == v) {
        v->srv->grabbed_view = nullptr;
        v->srv->grab_mode = cursor_mode::passthrough;
        v->srv->grab_restore_pending = false;
    }
    if (v->srv->titlebar_last_click_view == v) {
        v->srv->titlebar_last_click_view = nullptr;
    }
    if (v->srv->hovered_deco_view == v) {
        v->srv->hovered_deco_view = nullptr;
        v->srv->hovered_deco_node = nullptr;
    }

    // unfocus keyboard if is focused currently
    if (v->srv->seat->keyboard_state.focused_surface == v->toplevel->base->surface) {
        wlr_seat_keyboard_notify_clear_focus(v->srv->seat);
    }

    // unfocus pointer if is focused currently
    if (v->srv->seat->pointer_state.focused_surface == v->toplevel->base->surface) {
        wlr_seat_pointer_clear_focus(v->srv->seat);
    }

    // destroy foreign toplevel handles
    v->ft_request_maximize.disconnect();
    v->ft_request_minimize.disconnect();
    v->ft_request_activate.disconnect();
    v->ft_request_fullscreen.disconnect();
    v->ft_request_close.disconnect();
    if (v->foreign_handle) {
        wlr_foreign_toplevel_handle_v1_destroy(v->foreign_handle);
        v->foreign_handle = nullptr;
    }
    if (v->foreign_ext_handle) {
        wlr_ext_foreign_toplevel_handle_v1_destroy(v->foreign_ext_handle);
        v->foreign_ext_handle = nullptr;
    }

    // set properties
    v->mapped = false;
    wl_list_remove(&v->link);
    wl_list_init(&v->link);
    wlr_scene_node_set_enabled(&v->scene_tree->node, false);

    idle_inhibitor::update(v->srv);

    switcher::view_removed(v->srv, v);

    // remove view from all taskbars
    output* out;
    // update for each output's taskbar
    wl_list_for_each(out, &v->srv->outputs, link) {
        if (out->output_taskbar) {
            out->output_taskbar->view_removed(v);
        }
    }

    // focus next window
    view::focus_next(v->srv, v);
}

// update/render window
void view::handle_commit(view* v) {
    if (v->toplevel->base->initial_commit) {
        if (!v->initial_configure_idle) {
            struct wl_event_loop* event_loop = wl_display_get_event_loop(v->srv->display);
            v->initial_configure_idle = wl_event_loop_add_idle(event_loop, initial_configure, v);
        }
        return;
    }
    apply_pending_deco(v);
    v->deco_update();
}

// clean up view
void view::handle_destroy(view* v) {
    if (v->initial_configure_idle) {
        wl_event_source_remove(v->initial_configure_idle);
    }

    // remove from view list
    if (v->mapped) {
        wl_list_remove(&v->link);
    }

    // clear grab on window
    if (v->srv->grabbed_view == v) {
        v->srv->grabbed_view = nullptr;
        v->srv->grab_mode = cursor_mode::passthrough;
        v->srv->grab_restore_pending = false;
    }
    if (v->srv->titlebar_last_click_view == v) {
        v->srv->titlebar_last_click_view = nullptr;
    }

    // clear kb focus
    if (v->srv->seat->keyboard_state.focused_surface == v->toplevel->base->surface) {
        wlr_seat_keyboard_notify_clear_focus(v->srv->seat);
    }

    // clear cursor focus
    if (v->srv->seat->pointer_state.focused_surface == v->toplevel->base->surface) {
        wlr_seat_pointer_clear_focus(v->srv->seat);
    }

    // destroy foreign toplevel handles
    v->ft_request_maximize.disconnect();
    v->ft_request_minimize.disconnect();
    v->ft_request_activate.disconnect();
    v->ft_request_fullscreen.disconnect();
    v->ft_request_close.disconnect();
    if (v->foreign_handle) {
        wlr_foreign_toplevel_handle_v1_destroy(v->foreign_handle);
        v->foreign_handle = nullptr;
    }
    if (v->foreign_ext_handle) {
        wlr_ext_foreign_toplevel_handle_v1_destroy(v->foreign_ext_handle);
        v->foreign_ext_handle = nullptr;
    }

    // set properties
    v->toplevel->base->data = nullptr;

    if (v->icon) {
        wlr_xdg_toplevel_icon_v1_unref(v->icon);
        v->icon = nullptr;
    }
    v->deco_destroy();
    wlr_scene_node_destroy(&v->scene_tree->node);
    delete v;
}

// when the client wants to move or resize a window
void view::handle_request_move(view* v) {
    server::cursor_begin_interactive(v, cursor_mode::move, 0);
}

void view::handle_request_resize(view* v, void* data) {
    struct wlr_xdg_toplevel_resize_event* event =
        static_cast<struct wlr_xdg_toplevel_resize_event*>(data);
    server::cursor_begin_interactive(v, cursor_mode::resize, event->edges);
}

// maximize/full screen and save the old geometry
void view::apply_state(view* v, bool maximized, bool fullscreen) {
    // a client may request something before initial commit, so don't accept it
    if (!can_configure(v)) {
        return;
    }

    // stuff
    server* s = v->srv;
    struct wlr_scene_node* node = &v->scene_tree->node;
    bool was_special = v->maximized || v->fullscreen || v->snapped != snap_edge::NONE;
    bool was_fullscreen = v->fullscreen;
    bool now_special = maximized || fullscreen;

    // save the current geometry
    if (now_special) {
        if (!was_special) {
            // save the current geometry before maximizing
            struct wlr_box* geo = &v->toplevel->base->geometry;
            v->saved_geo = (struct wlr_box) {
                .x = node->x,
                .y = node->y,
                .width = geo->width,
                .height = geo->height,
            };
        }

        // fill the output with the view (window)
        struct wlr_output* wlr_out =
            wlr_output_layout_output_at(s->output_layout, s->cursor->x, s->cursor->y);
        if (!wlr_out) {
            // find output under cursor otherwise use the current output of the window
            wlr_out = wlr_output_layout_output_at(s->output_layout, node->x, node->y);
        }
        if (fullscreen) {
            // fullscreen covers the taskbar, so it gets the whole output
            struct wlr_box out_box;
            wlr_output_layout_get_box(s->output_layout, wlr_out, &out_box);
            wlr_scene_node_set_position(node, out_box.x, out_box.y);
            wlr_scene_node_set_position(&v->xdg_tree->node, 0, 0);
            wlr_xdg_toplevel_set_size(v->toplevel, out_box.width, out_box.height);
        } else {
            // maximized stops at the taskbar, and starts below it if the bar is at the top
            const wlr_box work = output::usable_area(s, wlr_out);
            int ox = v->decoration_mode == deco_mode::SERVER ? s->cfg.border_w : 0;
            int oy = v->decoration_mode == deco_mode::SERVER ? s->cfg.title_h : 0;
            wlr_scene_node_set_position(node, work.x, work.y);
            wlr_scene_node_set_position(&v->xdg_tree->node, ox, oy);
            wlr_xdg_toplevel_set_size(v->toplevel, work.width - 2 * ox, work.height - oy - ox);
        }

        // restore state if we are exiting
    } else if (was_special) {
        int ox = v->decoration_mode == deco_mode::SERVER ? s->cfg.border_w : 0;
        int oy = v->decoration_mode == deco_mode::SERVER ? s->cfg.title_h : 0;
        wlr_scene_node_set_position(&v->xdg_tree->node, ox, oy);
        wlr_scene_node_set_position(node, v->saved_geo.x, v->saved_geo.y);
        wlr_xdg_toplevel_set_size(v->toplevel, v->saved_geo.width, v->saved_geo.height);
    }

    // update state and notify the client
    v->maximized = maximized;
    v->fullscreen = fullscreen;
    v->snapped = snap_edge::NONE;
    wlr_xdg_toplevel_set_maximized(v->toplevel, maximized);
    wlr_xdg_toplevel_set_fullscreen(v->toplevel, fullscreen);
    wlr_xdg_toplevel_set_tiled(v->toplevel, 0);
    if (v->foreign_handle) {
        wlr_foreign_toplevel_handle_v1_set_maximized(v->foreign_handle, maximized);
        wlr_foreign_toplevel_handle_v1_set_fullscreen(v->foreign_handle, fullscreen);
    }

    // hide decorations and lift the window over the taskbar while fullscreen
    v->deco_set_visible(!fullscreen);
    if (fullscreen) {
        wlr_scene_node_raise_to_top(node);
    } else if (was_fullscreen) {
        raise_overlays(s);
    }

    // wayland brah
    if (can_configure(v)) {
        wlr_xdg_surface_schedule_configure(v->toplevel->base);
    }
}

// maximize a view
void view::handle_request_maximize(view* v) {
    apply_state(v, v->toplevel->requested.maximized, v->fullscreen);
}

// fullscreen a view
void view::handle_request_fullscreen(view* v) {
    apply_state(v, v->maximized, v->toplevel->requested.fullscreen);
}

// minimize a view, handing focus to the next visible window
void view::handle_request_minimize(view* v) {
    if (!v->toplevel->requested.minimized) {
        return;
    }
    v->minimize(true);

    // focus next window
    view::focus_next(v->srv, v);
}

void view::toggle_maximize() {
    apply_state(this, !maximized, fullscreen);
}

void view::toggle_fullscreen() {
    apply_state(this, maximized, !fullscreen);
}

// restore a maximized/snapped view under the cursor so it can be dragged
void view::unmaximize_to_cursor(double cursor_x, double cursor_y) {
    if (!maximized && !fullscreen && snapped == snap_edge::NONE) {
        return;
    }

    // fraction of the cursor across the current decorated width
    struct wlr_box cur;
    get_box(this, &cur);
    double frac_x = cur.width > 0 ? (cursor_x - cur.x) / cur.width : 0.0;
    if (frac_x < 0.0) {
        frac_x = 0.0;
    }
    if (frac_x > 1.0) {
        frac_x = 1.0;
    }

    // restore to the saved geometry size
    if (snapped != snap_edge::NONE) {
        snap_to(snap_edge::NONE);
    } else {
        apply_state(this, false, false);
    }

    // place so the cursor keeps the same horizontal fraction and grabs the titlebar
    int bw = decoration_mode == deco_mode::SERVER ? srv->cfg.border_w : 0;
    int th = decoration_mode == deco_mode::SERVER ? srv->cfg.title_h : 0;
    int restored_w = saved_geo.width + 2 * bw;
    int nx = (int) (cursor_x - frac_x * restored_w);
    int ny = (int) (cursor_y - th / 2.0);
    wlr_scene_node_set_position(&scene_tree->node, nx, ny);
}

void view::reconfigure_all(server* s) {
    s->hovered_deco_view = nullptr;
    s->hovered_deco_node = nullptr;
    struct wlr_surface* focused = s->seat->keyboard_state.focused_surface;
    view* v;
    wl_list_for_each(v, &s->views, link) {
        if (v->decoration_mode != deco_mode::SERVER) {
            continue;
        }
        wlr_scene_node_set_position(&v->xdg_tree->node, s->cfg.border_w, s->cfg.title_h);
        v->deco_update();
        v->deco_set_focus(focused && v->toplevel->base->surface == focused);
    }
}

void view::init(server* s) {
    wl_list_init(&s->views);
    s->xdg_shell = wlr_xdg_shell_create(s->display, 6);

    s->xdg_activation = wlr_xdg_activation_v1_create(s->display);
    s->xdg_activation->token_timeout_msec = 30000;
    s->request_activate.notify = view::handle_activation_request;
    wl_signal_add(&s->xdg_activation->events.request_activate, &s->request_activate);

    s->new_xdg_toplevel.notify = view::on_new;
    wl_signal_add(&s->xdg_shell->events.new_toplevel, &s->new_xdg_toplevel);
    s->new_xdg_popup.notify = popup::on_new;
    wl_signal_add(&s->xdg_shell->events.new_popup, &s->new_xdg_popup);

    s->deco_manager = wlr_xdg_decoration_manager_v1_create(s->display);
    s->new_deco.notify = view::deco_new;
    wl_signal_add(&s->deco_manager->events.new_toplevel_decoration, &s->new_deco);

    // foreign-toplevel protocols
    s->foreign_toplevel_mgr = wlr_foreign_toplevel_manager_v1_create(s->display);
    s->foreign_toplevel_list = wlr_ext_foreign_toplevel_list_v1_create(s->display, 1);

    // xdg-toplevel-icon protocol
    s->icon_mgr = wlr_xdg_toplevel_icon_manager_v1_create(s->display, 1);
    int icon_sizes[] = {16, 24, 32, 48};
    wlr_xdg_toplevel_icon_manager_v1_set_sizes(s->icon_mgr, icon_sizes, 4);
    s->set_icon.notify = view::handle_set_icon;
    wl_signal_add(&s->icon_mgr->events.set_icon, &s->set_icon);
}

// create a new view
void view::on_new(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, new_xdg_toplevel);
    struct wlr_xdg_toplevel* toplevel = static_cast<struct wlr_xdg_toplevel*>(data);
    view* v = new view();

    v->srv = s;
    v->toplevel = toplevel;
    v->decoration_mode = deco_mode::CLIENT; // switched to SERVER by the client when it detects
                                            // ssd support (which we do)
    v->urgent = false;
    v->pinned = false;
    v->snapped = snap_edge::NONE;
    v->icon = nullptr;
    v->foreign_handle = nullptr;
    v->foreign_ext_handle = nullptr;

    // whole scene for the window (title bar, border, surface)
    v->scene_tree = wlr_scene_tree_create(&s->scene->tree);
    v->scene_tree->node.data = v;

    if (s->locked) {
        wlr_scene_node_raise_to_top(&s->lock_tree->node);
    }

    // initially at 0,0
    v->xdg_tree = wlr_scene_xdg_surface_create(v->scene_tree, toplevel->base);
    v->xdg_tree->node.data = v;
    toplevel->base->data = v;

    // wire up the toplevel events; each Listener removes itself in ~view
    v->map.connect(&toplevel->base->surface->events.map, [v](void*) { handle_map(v); });
    v->unmap.connect(&toplevel->base->surface->events.unmap, [v](void*) { handle_unmap(v); });
    v->commit.connect(&toplevel->base->surface->events.commit, [v](void*) { handle_commit(v); });
    v->destroy.connect(&toplevel->events.destroy, [v](void*) { handle_destroy(v); });
    v->request_move.connect(&toplevel->events.request_move, [v](void*) { handle_request_move(v); });
    v->request_resize.connect(&toplevel->events.request_resize,
                              [v](void* data) { handle_request_resize(v, data); });
    v->request_maximize.connect(&toplevel->events.request_maximize,
                                [v](void*) { handle_request_maximize(v); });
    v->request_fullscreen.connect(&toplevel->events.request_fullscreen,
                                  [v](void*) { handle_request_fullscreen(v); });
    v->request_minimize.connect(&toplevel->events.request_minimize,
                                [v](void*) { handle_request_minimize(v); });
    v->title_changed.connect(&toplevel->events.set_title, [v](void*) {
        v->deco_update();
        if (v->foreign_handle) {
            wlr_foreign_toplevel_handle_v1_set_title(v->foreign_handle, v->toplevel->title);
        }
        if (v->foreign_ext_handle) {
            const wlr_ext_foreign_toplevel_handle_v1_state ext = {
                .title = v->toplevel->title,
                .app_id = v->toplevel->app_id,
            };
            wlr_ext_foreign_toplevel_handle_v1_update_state(v->foreign_ext_handle, &ext);
        }
    });
    v->app_id_changed.connect(&toplevel->events.set_app_id, [v](void*) {
        if (v->foreign_handle) {
            wlr_foreign_toplevel_handle_v1_set_app_id(v->foreign_handle, v->toplevel->app_id);
        }
        if (v->foreign_ext_handle) {
            const wlr_ext_foreign_toplevel_handle_v1_state ext = {
                .title = v->toplevel->title,
                .app_id = v->toplevel->app_id,
            };
            wlr_ext_foreign_toplevel_handle_v1_update_state(v->foreign_ext_handle, &ext);
        }
    });
}

void view::handle_set_icon(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, set_icon);
    const auto* event = static_cast<struct wlr_xdg_toplevel_icon_manager_v1_set_icon_event*>(data);
    const auto v = static_cast<view*>(event->toplevel->base->data);
    if (!v) {
        return;
    }

    if (v->icon) {
        wlr_xdg_toplevel_icon_v1_unref(v->icon);
    }
    v->icon = event->icon ? wlr_xdg_toplevel_icon_v1_ref(event->icon) : nullptr;

    taskbar::refresh_taskbars(s);
}

view* view::from_surface(struct wlr_surface* surface) {
    if (!surface) {
        return nullptr;
    }
    struct wlr_surface* root = wlr_surface_get_root_surface(surface);
    struct wlr_xdg_surface* xdg = wlr_xdg_surface_try_from_wlr_surface(root);
    while (xdg && xdg->role == WLR_XDG_SURFACE_ROLE_POPUP && xdg->popup->parent) {
        root = wlr_surface_get_root_surface(xdg->popup->parent);
        xdg = wlr_xdg_surface_try_from_wlr_surface(root);
    }
    if (!xdg || xdg->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return nullptr;
    }
    return static_cast<view*>(xdg->toplevel->base->data);
}

bool view::activation_token_valid(server* s, struct wlr_xdg_activation_token_v1* token) {
    if (!token || token->seat != s->seat || !token->surface) {
        return false;
    }

    struct wlr_surface* focused = s->seat->keyboard_state.focused_surface;
    if (!focused) {
        return false;
    }
    if (from_surface(focused) != from_surface(token->surface)) {
        return false;
    }

    struct wl_client* client = wl_resource_get_client(token->surface->resource);
    struct wlr_seat_client* seat_client = wlr_seat_client_for_wl_client(token->seat, client);
    return seat_client && wlr_seat_client_validate_event_serial(seat_client, token->serial);
}

bool view::surface_is_view_focused(server* s, view* v) {
    struct wlr_surface* focused = s->seat->keyboard_state.focused_surface;
    if (!focused) {
        return false;
    }
    return from_surface(focused) == v;
}

void view::handle_activation_request(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, request_activate);
    auto* event = static_cast<struct wlr_xdg_activation_v1_request_activate_event*>(data);
    view* target = from_surface(event->surface);
    if (!target || !target->mapped) {
        return;
    }

    if (activation_token_valid(s, event->token)) {
        if (target->workspace != s->current_workspace) {
            workspace_switch(s, target->workspace);
        }
        target->focus(target->toplevel->base->surface);
    } else if (!surface_is_view_focused(s, target)) {
        target->set_urgent(true);
    }
}
bool popup::unconstrain(popup* p) {
    struct wlr_xdg_popup* xdg_popup = p->xdg_popup;
    if (!xdg_popup->base->initialized) {
        return false; // configuring before the initial commit would assert
    }

    // walk up the popup chain to the toplevel that owns a view
    struct wlr_xdg_surface* parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    while (parent && parent->role == WLR_XDG_SURFACE_ROLE_POPUP && parent->popup->parent) {
        parent = wlr_xdg_surface_try_from_wlr_surface(parent->popup->parent);
    }
    view* root = parent && parent->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL
                     ? static_cast<view*>(parent->data)
                     : nullptr;
    if (!root) {
        return false;
    }

    // output box translated into the root toplevel's surface coordinate system
    server* s = root->srv;
    int surf_lx = root->scene_tree->node.x + root->xdg_tree->node.x;
    int surf_ly = root->scene_tree->node.y + root->xdg_tree->node.y;
    struct wlr_output* wlr_out = wlr_output_layout_output_at(s->output_layout, surf_lx, surf_ly);
    if (!wlr_out) {
        return false;
    }
    struct wlr_box ob;
    wlr_output_layout_get_box(s->output_layout, wlr_out, &ob);
    struct wlr_box rel = {ob.x - surf_lx, ob.y - surf_ly, ob.width, ob.height};
    wlr_xdg_popup_unconstrain_from_box(xdg_popup, &rel);
    return true;
}

void popup::handle_commit(popup* p) {
    // unconstrain once, on the first commit after the surface is initialized
    if (!p->unconstrained) {
        p->unconstrained = unconstrain(p);
    }
}

void popup::handle_reposition(popup* p) {
    unconstrain(p);
}

void popup::handle_destroy(popup* p) {
    // the Listener members disconnect themselves in ~popup
    delete p;
}

void popup::on_new(struct wl_listener* listener, void* data) {
    (void) listener;
    struct wlr_xdg_popup* xdg_popup = static_cast<struct wlr_xdg_popup*>(data);
    if (!xdg_popup->parent) {
        return;
    }
    struct wlr_xdg_surface* parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    if (!parent) {
        return;
    }

    // find the scene tree to parent the popup under
    struct wlr_scene_tree* parent_tree = nullptr;
    if (parent->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        view* v = static_cast<view*>(parent->data);
        if (v) {
            parent_tree = v->xdg_tree;
        }
    } else if (parent->role == WLR_XDG_SURFACE_ROLE_POPUP) {
        parent_tree = static_cast<struct wlr_scene_tree*>(parent->data);
    }
    if (!parent_tree) {
        return;
    }

    xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    popup* p = new popup();
    p->xdg_popup = xdg_popup;
    p->commit.connect(&xdg_popup->base->surface->events.commit, [p](void*) { handle_commit(p); });
    p->reposition.connect(&xdg_popup->events.reposition, [p](void*) { handle_reposition(p); });
    p->destroy.connect(&xdg_popup->events.destroy, [p](void*) { handle_destroy(p); });
}

// find which view is at a certain coord, and return its steppewm_view
view* view::at(server* s, double lx, double ly, struct wlr_surface** surface, double* sx,
               double* sy) {

    // get the scene node
    struct wlr_scene_node* node = wlr_scene_node_at(&s->scene->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
        return nullptr;
    }

    // get the wayland surface from the node
    struct wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return nullptr;
    }
    *surface = scene_surface->surface;

    // find the view
    struct wlr_scene_tree* tree = node->parent;
    while (tree && !tree->node.data) {
        tree = tree->node.parent;
    }

    // return null if no data
    if (!tree) {
        return nullptr;
    }
    return static_cast<view*>(tree->node.data);
}

void view::raise_overlays(server* s) {
    view* v;
    // raise pinned windows
    wl_list_for_each(v, &s->views, link) {
        if (v->pinned && v->mapped) {
            wlr_scene_node_raise_to_top(&v->scene_tree->node);
        }
    }

    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        if (out->output_taskbar) {
            out->output_taskbar->raise();
        }
        for (int i = ZWLR_LAYER_SHELL_V1_LAYER_TOP; i <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; i++) {
            if (out->layer_trees[i]) {
                wlr_scene_node_raise_to_top(&out->layer_trees[i]->node);
            }
        }
    }
}

// focus a view
void view::focus(struct wlr_surface* surface) {
    if (!mapped) {
        return;
    }

    if (srv->locked) {
        return;
    }

    // restore if minimized
    if (minimized) {
        minimize(false);
    }
    set_urgent(false);

    // get objs
    server* s = srv;
    struct wlr_seat* seat = s->seat;
    struct wlr_surface* prev = seat->keyboard_state.focused_surface;

    // check if we are already focused, if so return
    if (prev == surface) {
        return;
    }

    // unfocus the previously focused window
    if (prev) {
        struct wlr_xdg_surface* xdg = wlr_xdg_surface_try_from_wlr_surface(prev);
        if (xdg && xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
            wlr_xdg_toplevel_set_activated(xdg->toplevel, false);
            auto prev_view = static_cast<view*>(xdg->toplevel->base->data);

            if (prev_view) {
                prev_view->deco_set_focus(false);
                if (prev_view->foreign_handle) {
                    wlr_foreign_toplevel_handle_v1_set_activated(prev_view->foreign_handle, false);
                }
            }
        }
    }

    // activate new view
    wlr_scene_node_raise_to_top(&scene_tree->node);
    wl_list_remove(&link);
    wl_list_insert(&s->views, &link);
    wlr_xdg_toplevel_set_activated(toplevel, true);
    if (foreign_handle) {
        wlr_foreign_toplevel_handle_v1_set_activated(foreign_handle, true);
    }
    s->hovered_deco_view = nullptr;
    s->hovered_deco_node = nullptr;
    deco_set_focus(true);

    // notify the seat that the keyboard now focuses this view
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }

    // a window now owns keyboard focus, so no layer surface does
    s->focused_layer = nullptr;

    raise_overlays(s);

    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        if (out->output_taskbar) {
            out->output_taskbar->refresh();
        }
    }

    if (fullscreen) {
        wlr_scene_node_raise_to_top(&scene_tree->node);
    }
}
