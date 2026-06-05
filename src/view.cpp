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

#include "deco.h"
#include "input.h"
#include "output.h"
#include "server.h"
#include "taskbar.h"
#include "view.h"

static bool view_can_configure(struct steppewm_view *view) {
    return view->toplevel->base->initialized;
}

// refresh every output's taskbar
static void refresh_taskbars(struct steppewm_server *server) {
    struct steppewm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        if (out->taskbar) {
            taskbar_refresh(out->taskbar);
        }
    }
}

// show or hide a view depending on whether it lives on the current workspace
void view_update_visibility(struct steppewm_view *view) {
    bool visible = !view->minimized && view->workspace == view->server->current_workspace;
    wlr_scene_node_set_enabled(&view->scene_tree->node, visible);
}

void view_focus_next(struct steppewm_server *server, struct steppewm_view *skip) {
    struct steppewm_view *next = nullptr;
    struct steppewm_view *view;

    // loop through views until we find one on this workspace that isn't minimzed
    wl_list_for_each(view, &server->views, link) {
        if (view != skip && view->mapped && !view->minimized &&
            view->workspace == server->current_workspace) {
            next = view;
            break;
        }
    }

    // focus the next window, otherwise clear focus
    if (next) {
        view_focus(next, next->toplevel->base->surface);
    } else {
        wlr_seat_keyboard_notify_clear_focus(server->seat);
        refresh_taskbars(server);
    }
}

// minimize a view and hide it from the scene
void view_minimize(struct steppewm_view *view, bool minimized) {
    view->minimized = minimized;
    view_update_visibility(view);
    refresh_taskbars(view->server);
}

// switch the visible workspace, hiding the old set and showing the new one
void workspace_switch(struct steppewm_server *server, int workspace) {
    if (workspace < 0 || workspace >= STEPPEWM_NUM_WORKSPACES ||
        workspace == server->current_workspace) {
        return;
    }
    server->current_workspace = workspace;

    // toggle visibility of every window for the new workspace
    struct steppewm_view *view;
    wl_list_for_each(view, &server->views, link) {
        view_update_visibility(view);
    }

    // focus the topmost window on the new workspace, otherwise clear focus
    struct steppewm_view *focus = nullptr;
    wl_list_for_each(view, &server->views, link) {
        if (view->mapped && !view->minimized && view->workspace == workspace) {
            focus = view;
            break;
        }
    }
    if (focus) {
        view_focus(focus, focus->toplevel->base->surface);
    } else {
        wlr_seat_keyboard_notify_clear_focus(server->seat);
    }

    // redraw taskbars so the active workspace and window list update
    refresh_taskbars(server);
}

// move a window to another workspace
void view_move_to_workspace(struct steppewm_view *view, int workspace) {
    if (workspace < 0 || workspace >= STEPPEWM_NUM_WORKSPACES || view->workspace == workspace) {
        return;
    }
    view->workspace = workspace;
    view_update_visibility(view);

    // the window left the current workspace, hand focus to a remaining one
    view_focus_next(view->server, view);
    refresh_taskbars(view->server);
}

// apply ssd deco
static void view_apply_pending_deco(struct steppewm_view *view) {
    if (!view->pending_deco || !view_can_configure(view)) {
        return;
    }

    wlr_xdg_toplevel_decoration_v1_set_mode(view->pending_deco,
                                            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    view->deco_mode = STEPPEWM_DECO_SERVER;
    wlr_scene_node_set_position(&view->xdg_tree->node, view->server->config.border_w,
                                view->server->config.title_h);
    deco_create(view);
    view->pending_deco = nullptr;
}

// move
static void view_initial_configure(void *data) {
    struct steppewm_view* view = static_cast<struct steppewm_view*>(data);
    view->initial_configure_idle = nullptr;

    if (!view_can_configure(view)) {
        return;
    }

    view_apply_pending_deco(view);
    wlr_xdg_toplevel_set_size(view->toplevel, 0, 0);
}

// full decorated bounding box of a view at its current scene position
static void view_get_box(struct steppewm_view *view, struct wlr_box *box) {
    struct wlr_box *geo = &view->toplevel->base->geometry;
    int bw = view->deco_mode == STEPPEWM_DECO_SERVER ? view->server->config.border_w : 0;
    int th = view->deco_mode == STEPPEWM_DECO_SERVER ? view->server->config.title_h : 0;
    box->x = view->scene_tree->node.x;
    box->y = view->scene_tree->node.y;
    box->width = geo->width + 2 * bw;
    box->height = geo->height + th + bw;
}

// cascadeing
// each new window steps down right from the last
// when it gets to the bottom we go back to the top but sdhifted to the right
static void view_place(struct steppewm_view *view) {
    struct steppewm_server *server = view->server;

    // already-positioned states manage their own geometry
    if (view->maximized || view->fullscreen) {
        return;
    }

    // choose the output under the cursor, falling back to the first output
    struct wlr_output *output =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    if (!output) {
        struct steppewm_output *o;
        wl_list_for_each(o, &server->outputs, link) {
            output = o->wlr_output;
            break;
        }
    }
    if (!output) {
        return;
    }

    // usable area is output box minus the taskbar at the bottom
    struct wlr_box area;
    wlr_output_layout_get_box(server->output_layout, output, &area);
    area.height -= server->config.taskbar_h;
    if (area.height < 0) {
        area.height = 0;
    }

    // size of the window being placed with decorations
    struct wlr_box self;
    view_get_box(view, &self);
    int w = self.width;
    int h = self.height;

    // diagonal step
    // by default 23 px
    int step = server->config.title_h + server->config.border_w;

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
    int y_off = server->cascade_n * step;
    if (y_off > max_off_y) {
        // start a new column, offset to the right when hit to the bottom
        server->cascade_n = 0;
        y_off = 0;
        server->cascade_x += col_step;
    }
    // columns filled the width, wrap back to the left edge
    if (server->cascade_x > max_off_x) {
        server->cascade_x = 0;
    }

    // diagonal offset down the column
    int x_off = server->cascade_x + server->cascade_n * step;
    if (x_off > max_off_x) {
        x_off = max_off_x;
    }
    server->cascade_n++;

    wlr_scene_node_set_position(&view->scene_tree->node, area.x + x_off, area.y + y_off);
}

// focus a new window
static void view_map(struct steppewm_view* view) {
    // new windows open on the currently visible workspace
    view->mapped = true;
    view->workspace = view->server->current_workspace;
    wl_list_insert(&view->server->views, &view->link);
    view_update_visibility(view);

    // position the window to avoid overlap
    view_place(view);

    // add window to all taskbars
    struct steppewm_output *out;
    // update for each output's taskbar
    wl_list_for_each(out, &view->server->outputs, link) {
        if (out->taskbar) {
            taskbar_view_added(out->taskbar, view);
        }
    }
    view_focus(view, view->toplevel->base->surface);
}

// remove window (view)
static void view_unmap(struct steppewm_view* view) {
    if (view->server->grabbed_view == view) {
        view->server->grabbed_view = nullptr;
        view->server->cursor_mode = STEPPEWM_CURSOR_PASSTHROUGH;
        view->server->grab_restore_pending = false;
    }

    // unfocus keyboard if is focused currently
    if (view->server->seat->keyboard_state.focused_surface == view->toplevel->base->surface) {
        wlr_seat_keyboard_notify_clear_focus(view->server->seat);
    }

    // unfocus pointer if is focused currently
    if (view->server->seat->pointer_state.focused_surface == view->toplevel->base->surface) {
        wlr_seat_pointer_clear_focus(view->server->seat);
    }

    // set properties
    view->mapped = false;
    wl_list_remove(&view->link);
    wl_list_init(&view->link);
    wlr_scene_node_set_enabled(&view->scene_tree->node, false);

    // remove view from all taskbars
    struct steppewm_output *out2;
    // update for each output's taskbar
    wl_list_for_each(out2, &view->server->outputs, link) {
        if (out2->taskbar) {
            taskbar_view_removed(out2->taskbar, view);
        }
    }

    // focus next window
    view_focus_next(view->server, view);
}

// update/render window
static void view_commit(struct steppewm_view* view) {
    if (view->toplevel->base->initial_commit) {
        if (!view->initial_configure_idle) {
            struct wl_event_loop *event_loop = wl_display_get_event_loop(view->server->display);
            view->initial_configure_idle =
                wl_event_loop_add_idle(event_loop, view_initial_configure, view);
        }
        return;
    }
    view_apply_pending_deco(view);
    deco_update(view);
}

// clean up view
static void view_destroy(struct steppewm_view* view) {
    if (view->initial_configure_idle) {
        wl_event_source_remove(view->initial_configure_idle);
    }

    // remove from view list
    if (view->mapped) {
        wl_list_remove(&view->link);
    }

    // clear grab on window
    if (view->server->grabbed_view == view) {
        view->server->grabbed_view = nullptr;
        view->server->cursor_mode = STEPPEWM_CURSOR_PASSTHROUGH;
        view->server->grab_restore_pending = false;
    }

    // clear kb focus
    if (view->server->seat->keyboard_state.focused_surface == view->toplevel->base->surface) {
        wlr_seat_keyboard_notify_clear_focus(view->server->seat);
    }

    // clear cursor focus
    if (view->server->seat->pointer_state.focused_surface == view->toplevel->base->surface) {
        wlr_seat_pointer_clear_focus(view->server->seat);
    }

    // set properties
    view->toplevel->base->data = nullptr;

    // the steppe::Listener members (incl. the deco ones) disconnect themselves
    // when ~steppewm_view runs, so there is no hand-rolled teardown here
    deco_destroy(view);
    wlr_scene_node_destroy(&view->scene_tree->node);
    delete view;
}

// called when title of window changed
// updates decorations
static void view_title_changed(struct steppewm_view* view) {
    deco_update(view);
}

// when the client wants to move or resize a window
static void view_request_move(struct steppewm_view* view) {
    cursor_begin_interactive(view, STEPPEWM_CURSOR_MOVE, 0);
}

static void view_request_resize(struct steppewm_view* view, void* data) {
    struct wlr_xdg_toplevel_resize_event* event =
        static_cast<struct wlr_xdg_toplevel_resize_event*>(data);
    cursor_begin_interactive(view, STEPPEWM_CURSOR_RESIZE, event->edges);
}

// maximize/full screen and save the old geometry
static void view_apply_state(struct steppewm_view *view, bool maximized, bool fullscreen) {
    // stuff
    struct steppewm_server *server = view->server;
    struct wlr_scene_node *node = &view->scene_tree->node;
    bool was_special = view->maximized || view->fullscreen;
    bool now_special = maximized || fullscreen;

    // save the current geometry
    if (now_special) {
        if (!was_special) {
            // save the current geometry before maximizing
            struct wlr_box *geo = &view->toplevel->base->geometry;
            view->saved_geo = (struct wlr_box) {
                .x = node->x,
                .y = node->y,
                .width = geo->width,
                .height = geo->height,
            };
        }

        // fill the output with the view (window)
        struct wlr_output *output = wlr_output_layout_output_at(
            server->output_layout, server->cursor->x, server->cursor->y);
        if (!output) {
            output = wlr_output_layout_output_at(server->output_layout, node->x, node->y);
        }
        struct wlr_box out_box;

        // find output under cursor otherwise use the current output of the window
        wlr_output_layout_get_box(server->output_layout, output, &out_box);
        wlr_scene_node_set_position(node, out_box.x, out_box.y);

        int ox = view->deco_mode == STEPPEWM_DECO_SERVER ? view->server->config.border_w : 0;
        int oy = view->deco_mode == STEPPEWM_DECO_SERVER ? view->server->config.title_h : 0;
        int bar_h = maximized && !fullscreen ? view->server->config.taskbar_h : 0;
        wlr_xdg_toplevel_set_size(view->toplevel, out_box.width - 2 * ox,
                                  out_box.height - oy - ox - bar_h);

        // restore state if we are exiting
    } else if (was_special) {
        wlr_scene_node_set_position(node, view->saved_geo.x, view->saved_geo.y);
        wlr_xdg_toplevel_set_size(view->toplevel, view->saved_geo.width, view->saved_geo.height);
    }

    // update state and notify the client
    view->maximized = maximized;
    view->fullscreen = fullscreen;
    wlr_xdg_toplevel_set_maximized(view->toplevel, maximized);
    wlr_xdg_toplevel_set_fullscreen(view->toplevel, fullscreen);
    // wayland brah
    if (view_can_configure(view)) {
        wlr_xdg_surface_schedule_configure(view->toplevel->base);
    }
}

// maximize a view
static void view_request_maximize(struct steppewm_view* view) {
    view_apply_state(view, view->toplevel->requested.maximized, view->fullscreen);
}

// fullscreen a view
static void view_request_fullscreen(struct steppewm_view* view) {
    view_apply_state(view, view->maximized, view->toplevel->requested.fullscreen);
}

// minimize a view, handing focus to the next visible window
static void view_on_request_minimize(struct steppewm_view* view) {
    if (!view->toplevel->requested.minimized) {
        return;
    }
    view_minimize(view, true);

    // focus next window
    view_focus_next(view->server, view);
}

void view_toggle_maximize(struct steppewm_view *view) {
    view_apply_state(view, !view->maximized, view->fullscreen);
}

// restore a maximized view under the cursor so it can be dragged
void view_unmaximize_to_cursor(struct steppewm_view *view, double cursor_x, double cursor_y) {
    if (!view->maximized && !view->fullscreen) {
        return;
    }

    // fraction of the cursor across the current decorated width
    struct wlr_box cur;
    view_get_box(view, &cur);
    double frac_x = cur.width > 0 ? (cursor_x - cur.x) / cur.width : 0.0;
    if (frac_x < 0.0) {
        frac_x = 0.0;
    }
    if (frac_x > 1.0) {
        frac_x = 1.0;
    }

    // restore to the saved geometry size
    view_apply_state(view, false, false);

    // place so the cursor keeps the same horizontal fraction and grabs the titlebar
    int bw = view->deco_mode == STEPPEWM_DECO_SERVER ? view->server->config.border_w : 0;
    int th = view->deco_mode == STEPPEWM_DECO_SERVER ? view->server->config.title_h : 0;
    int restored_w = view->saved_geo.width + 2 * bw;
    int nx = (int) (cursor_x - frac_x * restored_w);
    int ny = (int) (cursor_y - th / 2.0);
    wlr_scene_node_set_position(&view->scene_tree->node, nx, ny);
}

void view_reconfigure_all(struct steppewm_server *server) {
    struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
    struct steppewm_view *view;
    wl_list_for_each(view, &server->views, link) {
        if (view->deco_mode != STEPPEWM_DECO_SERVER) {
            continue;
        }
        wlr_scene_node_set_position(&view->xdg_tree->node, server->config.border_w,
                                    server->config.title_h);
        deco_update(view);
        deco_set_focus(view, focused && view->toplevel->base->surface == focused);
    }
}

// create a new view
void view_new(struct wl_listener *listener, void *data) {
    // get objects
    struct steppewm_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel* toplevel = static_cast<struct wlr_xdg_toplevel*>(data);
    struct steppewm_view* view = new steppewm_view();

    view->server = server;
    view->toplevel = toplevel;
    view->deco_mode = STEPPEWM_DECO_CLIENT; // switched to SERVER by the client when it detects
                                            // ssd support (which we do)

    // whole scene for the window (title bar, border, surface)
    view->scene_tree = wlr_scene_tree_create(&server->scene->tree);
    view->scene_tree->node.data = view;

    // initially at 0,0
    view->xdg_tree = wlr_scene_xdg_surface_create(view->scene_tree, toplevel->base);
    view->xdg_tree->node.data = view;
    toplevel->base->data = view; // for lookup by deco_new

    // wire up the toplevel events; each Listener removes itself in ~steppewm_view
    view->map.connect(&toplevel->base->surface->events.map, [view](void*) { view_map(view); });
    view->unmap.connect(&toplevel->base->surface->events.unmap,
                        [view](void*) { view_unmap(view); });
    view->commit.connect(&toplevel->base->surface->events.commit,
                         [view](void*) { view_commit(view); });
    view->destroy.connect(&toplevel->events.destroy, [view](void*) { view_destroy(view); });
    view->request_move.connect(&toplevel->events.request_move,
                               [view](void*) { view_request_move(view); });
    view->request_resize.connect(&toplevel->events.request_resize,
                                 [view](void* data) { view_request_resize(view, data); });
    view->request_maximize.connect(&toplevel->events.request_maximize,
                                   [view](void*) { view_request_maximize(view); });
    view->request_fullscreen.connect(&toplevel->events.request_fullscreen,
                                     [view](void*) { view_request_fullscreen(view); });
    view->request_minimize.connect(&toplevel->events.request_minimize,
                                   [view](void*) { view_on_request_minimize(view); });
    view->title_changed.connect(&toplevel->events.set_title,
                                [view](void*) { view_title_changed(view); });
}

// place the popup so it fits on the output holding its root toplevel
// returns true once it has actually been placed (only possible after the initial commit)
static bool popup_unconstrain(struct steppewm_popup *p) {
    struct wlr_xdg_popup *popup = p->popup;
    if (!popup->base->initialized) {
        return false; // configuring before the initial commit would assert
    }

    // walk up the popup chain to the toplevel that owns a steppewm_view
    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    while (parent && parent->role == WLR_XDG_SURFACE_ROLE_POPUP && parent->popup->parent) {
        parent = wlr_xdg_surface_try_from_wlr_surface(parent->popup->parent);
    }
    struct steppewm_view *root =
        parent && parent->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL
                                     ? static_cast<struct steppewm_view*>(parent->data)
                                     : nullptr;
    if (!root) {
        return false;
    }

    // output box translated into the root toplevel's surface coordinate system
    struct steppewm_server *server = root->server;
    int surf_lx = root->scene_tree->node.x + root->xdg_tree->node.x;
    int surf_ly = root->scene_tree->node.y + root->xdg_tree->node.y;
    struct wlr_output *output =
        wlr_output_layout_output_at(server->output_layout, surf_lx, surf_ly);
    if (!output) {
        return false;
    }
    struct wlr_box ob;
    wlr_output_layout_get_box(server->output_layout, output, &ob);
    struct wlr_box rel = {ob.x - surf_lx, ob.y - surf_ly, ob.width, ob.height};
    wlr_xdg_popup_unconstrain_from_box(popup, &rel);
    return true;
}

static void popup_commit(struct steppewm_popup *p) {
    // unconstrain once, on the first commit after the surface is initialized
    if (!p->unconstrained) {
        p->unconstrained = popup_unconstrain(p);
    }
}

static void popup_reposition(struct steppewm_popup* p) {
    popup_unconstrain(p);
}

static void popup_destroy(struct steppewm_popup* p) {
    // the Listener members disconnect themselves in ~steppewm_popup
    delete p;
}

void popup_new(struct wl_listener * listener, void* data) {
    (void) listener;
    struct wlr_xdg_popup* popup = static_cast<struct wlr_xdg_popup *>(data);
    if (!popup->parent) {
        return;
    }
    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (!parent) {
        return;
    }

    // find the scene tree to parent the popup under
    struct wlr_scene_tree *parent_tree = nullptr;
    if (parent->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        struct steppewm_view* view = static_cast<struct steppewm_view *>(parent->data);
        if (view) {
            parent_tree = view->xdg_tree;
        }
    } else if (parent->role == WLR_XDG_SURFACE_ROLE_POPUP) {
        parent_tree = static_cast<struct wlr_scene_tree *>(parent->data);
    }
    if (!parent_tree) {
        return;
    }

    popup->base->data = wlr_scene_xdg_surface_create(parent_tree, popup->base);

    struct steppewm_popup *p = new steppewm_popup();
    p->popup = popup;
    p->commit.connect(&popup->base->surface->events.commit, [p](void*) { popup_commit(p); });
    p->reposition.connect(&popup->events.reposition, [p](void*) { popup_reposition(p); });
    p->destroy.connect(&popup->events.destroy, [p](void *) { popup_destroy(p); });
}

// find which view is at a certain coord, and return its steppewm_view
struct steppewm_view *view_at(struct steppewm_server *server, double lx, double ly,
                              struct wlr_surface **surface, double *sx, double *sy) {

    // get the scene node
    struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
        return nullptr;
    }

    // get the wayland surface from the node
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return nullptr;
    }
    *surface = scene_surface->surface;

    // find the view
    struct wlr_scene_tree *tree = node->parent;
    while (tree && !tree->node.data) {
        tree = tree->node.parent;
    }

    // return null if no data
    if (!tree) {
        return nullptr;
    }
    return static_cast<struct steppewm_view *>(tree->node.data);
}

// focus a view
void view_focus(struct steppewm_view *view, struct wlr_surface *surface) {
    // if no view was provided or if view was not mapped
    if (!view || !view->mapped) {
        return;
    }

    // restore if minimized
    if (view->minimized) {
        view_minimize(view, false);
    }

    // get objs
    struct steppewm_server *server = view->server;
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *prev = seat->keyboard_state.focused_surface;

    // check if we are already focused, if so return
    if (prev == surface) {
        return;
    }

    // unfocus the previously focused window
    if (prev) {
        struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(prev);
        if (xdg && xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
            wlr_xdg_toplevel_set_activated(xdg->toplevel, false);
            struct steppewm_view* prev_view =
                static_cast<struct steppewm_view*>(xdg->toplevel->base->data);

            // unfocus it
            if (prev_view) {
                deco_set_focus(prev_view, false);
            }
        }
    }

    // activate new view
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
    wl_list_remove(&view->link);
    wl_list_insert(&server->views, &view->link);
    wlr_xdg_toplevel_set_activated(view->toplevel, true);
    deco_set_focus(view, true);

    // notify the seat that the keyboard now focuses this view
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }

    // a window now owns keyboard focus, so no layer surface does
    server->focused_layer = nullptr;

    struct steppewm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        if (out->taskbar) {
            taskbar_raise(out->taskbar);
            taskbar_refresh(out->taskbar);
        }
        // keep TOP and OVERLAY layer surfaces above taskbar
        for (int i = ZWLR_LAYER_SHELL_V1_LAYER_TOP; i <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; i++) {
            if (out->layer_trees[i]) {
                wlr_scene_node_raise_to_top(&out->layer_trees[i]->node);
            }
        }
    }
}
