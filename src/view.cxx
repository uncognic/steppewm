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

#include "wlr.hxx" // must be first

#include "deco.hxx"
#include "input.hxx"
#include "output.hxx"
#include "server.hxx"
#include "switcher.hxx"
#include "taskbar.hxx"
#include "view.hxx"

using namespace steppewm;

static bool view_can_configure(view* v) {
    return v->toplevel->base->initialized;
}

// refresh every output's taskbar
static void refresh_taskbars(server* s) {
    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        if (out->taskbar) {
            out->taskbar->refresh();
        }
    }
}

// show or hide a view depending on whether it lives on the current workspace
void view::update_visibility() {
    bool visible = !minimized && workspace == srv->current_workspace;
    wlr_scene_node_set_enabled(&scene_tree->node, visible);

    // hidden windows shouldn't keep blocking idle (and vice versa)
    idle_inhibitor::update(srv);
}

void view::focus_next(server* s, view* skip) {
    if (s->locked) {
        return;
    }

    view* next = nullptr;
    view* v;

    // loop through views until we find one on this workspace that isn't minimzed
    wl_list_for_each(v, &s->views, link) {
        if (v != skip && v->mapped && !v->minimized && v->workspace == s->current_workspace) {
            next = v;
            break;
        }
    }

    // focus the next window, otherwise clear focus
    if (next) {
        next->focus(next->toplevel->base->surface);
    } else {
        wlr_seat_keyboard_notify_clear_focus(s->seat);
        refresh_taskbars(s);
    }
}

// minimize a view and hide it from the scene
void view::minimize(bool min) {
    minimized = min;
    update_visibility();
    refresh_taskbars(srv);
}

void view::set_urgent(bool is_urgent) {
    if (urgent == is_urgent) {
        return;
    }
    urgent = is_urgent;
    refresh_taskbars(srv);
}

// switch the visible workspace, hiding the old set and showing the new one
void steppewm::workspace_switch(server* s, int workspace) {
    if (workspace < 0 || workspace >= num_workspaces || workspace == s->current_workspace) {
        return;
    }
    s->current_workspace = workspace;

    switcher::cancel(s);

    // toggle visibility of every window for the new workspace
    view* v;
    wl_list_for_each(v, &s->views, link) {
        v->update_visibility();
    }

    // focus the topmost window on the new workspace, otherwise clear focus
    view* focus = nullptr;
    wl_list_for_each(v, &s->views, link) {
        if (v->mapped && !v->minimized && v->workspace == workspace) {
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
    refresh_taskbars(s);
}

// move a window to another workspace
void view::move_to_workspace(int ws) {
    if (ws < 0 || ws >= num_workspaces || workspace == ws) {
        return;
    }
    workspace = ws;
    update_visibility();

    // the window left the current workspace
    switcher::view_removed(srv, this);

    // the window left the current workspace, hand focus to a remaining one
    view::focus_next(srv, this);
    refresh_taskbars(srv);
}

// apply ssd deco
static void view_apply_pending_deco(view* v) {
    if (!v->pending_deco || !view_can_configure(v)) {
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
static void view_initial_configure(void* data) {
    view* v = static_cast<view*>(data);
    v->initial_configure_idle = nullptr;

    if (!view_can_configure(v)) {
        return;
    }

    view_apply_pending_deco(v);
    wlr_xdg_toplevel_set_size(v->toplevel, 0, 0);
}

// full decorated bounding box of a view at its current scene position
static void view_get_box(view* v, struct wlr_box* box) {
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
static void view_place(view* v) {
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

    // usable area is output box minus the taskbar at the bottom
    struct wlr_box area;
    wlr_output_layout_get_box(s->output_layout, wlr_out, &area);
    area.height -= s->cfg.taskbar_h;
    if (area.height < 0) {
        area.height = 0;
    }

    // size of the window being placed with decorations
    struct wlr_box self;
    view_get_box(v, &self);
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
static void view_map(view* v) {
    // new windows open on the currently visible workspace
    v->mapped = true;
    v->workspace = v->srv->current_workspace;
    wl_list_insert(&v->srv->views, &v->link);
    v->update_visibility();

    // position the window to avoid overlap
    view_place(v);

    // add window to all taskbars
    output* out;
    // update for each output's taskbar
    wl_list_for_each(out, &v->srv->outputs, link) {
        if (out->taskbar) {
            out->taskbar->view_added(v);
        }
    }
    v->focus(v->toplevel->base->surface);
}

// remove window (view)
static void view_unmap(view* v) {
    if (v->srv->grabbed_view == v) {
        v->srv->grabbed_view = nullptr;
        v->srv->grab_mode = cursor_mode::PASSTHROUGH;
        v->srv->grab_restore_pending = false;
    }

    // unfocus keyboard if is focused currently
    if (v->srv->seat->keyboard_state.focused_surface == v->toplevel->base->surface) {
        wlr_seat_keyboard_notify_clear_focus(v->srv->seat);
    }

    // unfocus pointer if is focused currently
    if (v->srv->seat->pointer_state.focused_surface == v->toplevel->base->surface) {
        wlr_seat_pointer_clear_focus(v->srv->seat);
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
        if (out->taskbar) {
            out->taskbar->view_removed(v);
        }
    }

    // focus next window
    view::focus_next(v->srv, v);
}

// update/render window
static void view_commit(view* v) {
    if (v->toplevel->base->initial_commit) {
        if (!v->initial_configure_idle) {
            struct wl_event_loop* event_loop = wl_display_get_event_loop(v->srv->display);
            v->initial_configure_idle =
                wl_event_loop_add_idle(event_loop, view_initial_configure, v);
        }
        return;
    }
    view_apply_pending_deco(v);
    v->deco_update();
}

// clean up view
static void view_destroy(view* v) {
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
        v->srv->grab_mode = cursor_mode::PASSTHROUGH;
        v->srv->grab_restore_pending = false;
    }

    // clear kb focus
    if (v->srv->seat->keyboard_state.focused_surface == v->toplevel->base->surface) {
        wlr_seat_keyboard_notify_clear_focus(v->srv->seat);
    }

    // clear cursor focus
    if (v->srv->seat->pointer_state.focused_surface == v->toplevel->base->surface) {
        wlr_seat_pointer_clear_focus(v->srv->seat);
    }

    // set properties
    v->toplevel->base->data = nullptr;

    v->deco_destroy();
    wlr_scene_node_destroy(&v->scene_tree->node);
    delete v;
}

// when the client wants to move or resize a window
static void view_request_move(view* v) {
    cursor_begin_interactive(v, cursor_mode::MOVE, 0);
}

static void view_request_resize(view* v, void* data) {
    struct wlr_xdg_toplevel_resize_event* event =
        static_cast<struct wlr_xdg_toplevel_resize_event*>(data);
    cursor_begin_interactive(v, cursor_mode::RESIZE, event->edges);
}

// maximize/full screen and save the old geometry
static void view_apply_state(view* v, bool maximized, bool fullscreen) {
    // stuff
    server* s = v->srv;
    struct wlr_scene_node* node = &v->scene_tree->node;
    bool was_special = v->maximized || v->fullscreen;
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
        struct wlr_box out_box;

        wlr_output_layout_get_box(s->output_layout, wlr_out, &out_box);
        wlr_scene_node_set_position(node, out_box.x, out_box.y);

        int ox = v->decoration_mode == deco_mode::SERVER ? s->cfg.border_w : 0;
        int oy = v->decoration_mode == deco_mode::SERVER ? s->cfg.title_h : 0;
        int bar_h = maximized && !fullscreen ? s->cfg.taskbar_h : 0;
        wlr_xdg_toplevel_set_size(v->toplevel, out_box.width - 2 * ox,
                                  out_box.height - oy - ox - bar_h);

        // restore state if we are exiting
    } else if (was_special) {
        wlr_scene_node_set_position(node, v->saved_geo.x, v->saved_geo.y);
        wlr_xdg_toplevel_set_size(v->toplevel, v->saved_geo.width, v->saved_geo.height);
    }

    // update state and notify the client
    v->maximized = maximized;
    v->fullscreen = fullscreen;
    wlr_xdg_toplevel_set_maximized(v->toplevel, maximized);
    wlr_xdg_toplevel_set_fullscreen(v->toplevel, fullscreen);
    // wayland brah
    if (view_can_configure(v)) {
        wlr_xdg_surface_schedule_configure(v->toplevel->base);
    }
}

// maximize a view
static void view_request_maximize(view* v) {
    view_apply_state(v, v->toplevel->requested.maximized, v->fullscreen);
}

// fullscreen a view
static void view_request_fullscreen(view* v) {
    view_apply_state(v, v->maximized, v->toplevel->requested.fullscreen);
}

// minimize a view, handing focus to the next visible window
static void view_on_request_minimize(view* v) {
    if (!v->toplevel->requested.minimized) {
        return;
    }
    v->minimize(true);

    // focus next window
    view::focus_next(v->srv, v);
}

void view::toggle_maximize() {
    view_apply_state(this, !maximized, fullscreen);
}

// restore a maximized view under the cursor so it can be dragged
void view::unmaximize_to_cursor(double cursor_x, double cursor_y) {
    if (!maximized && !fullscreen) {
        return;
    }

    // fraction of the cursor across the current decorated width
    struct wlr_box cur;
    view_get_box(this, &cur);
    double frac_x = cur.width > 0 ? (cursor_x - cur.x) / cur.width : 0.0;
    if (frac_x < 0.0) {
        frac_x = 0.0;
    }
    if (frac_x > 1.0) {
        frac_x = 1.0;
    }

    // restore to the saved geometry size
    view_apply_state(this, false, false);

    // place so the cursor keeps the same horizontal fraction and grabs the titlebar
    int bw = decoration_mode == deco_mode::SERVER ? srv->cfg.border_w : 0;
    int th = decoration_mode == deco_mode::SERVER ? srv->cfg.title_h : 0;
    int restored_w = saved_geo.width + 2 * bw;
    int nx = (int) (cursor_x - frac_x * restored_w);
    int ny = (int) (cursor_y - th / 2.0);
    wlr_scene_node_set_position(&scene_tree->node, nx, ny);
}

void view::reconfigure_all(server* s) {
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
    v->map.connect(&toplevel->base->surface->events.map, [v](void*) { view_map(v); });
    v->unmap.connect(&toplevel->base->surface->events.unmap, [v](void*) { view_unmap(v); });
    v->commit.connect(&toplevel->base->surface->events.commit, [v](void*) { view_commit(v); });
    v->destroy.connect(&toplevel->events.destroy, [v](void*) { view_destroy(v); });
    v->request_move.connect(&toplevel->events.request_move, [v](void*) { view_request_move(v); });
    v->request_resize.connect(&toplevel->events.request_resize,
                              [v](void* data) { view_request_resize(v, data); });
    v->request_maximize.connect(&toplevel->events.request_maximize,
                                [v](void*) { view_request_maximize(v); });
    v->request_fullscreen.connect(&toplevel->events.request_fullscreen,
                                  [v](void*) { view_request_fullscreen(v); });
    v->request_minimize.connect(&toplevel->events.request_minimize,
                                [v](void*) { view_on_request_minimize(v); });
    v->title_changed.connect(&toplevel->events.set_title, [v](void*) { v->deco_update(); });
}

static view* view_from_surface(struct wlr_surface* surface) {
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

static bool activation_token_valid(server* s, struct wlr_xdg_activation_token_v1* token) {
    if (!token || token->seat != s->seat || !token->surface) {
        return false;
    }

    struct wlr_surface* focused = s->seat->keyboard_state.focused_surface;
    if (!focused) {
        return false;
    }
    if (view_from_surface(focused) != view_from_surface(token->surface)) {
        return false;
    }

    struct wl_client* client = wl_resource_get_client(token->surface->resource);
    struct wlr_seat_client* seat_client = wlr_seat_client_for_wl_client(token->seat, client);
    return seat_client && wlr_seat_client_validate_event_serial(seat_client, token->serial);
}

static bool surface_is_view_focused(server* s, view* v) {
    struct wlr_surface* focused = s->seat->keyboard_state.focused_surface;
    if (!focused) {
        return false;
    }
    return view_from_surface(focused) == v;
}

void view::handle_activation_request(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, request_activate);
    auto* event = static_cast<struct wlr_xdg_activation_v1_request_activate_event*>(data);
    view* target = view_from_surface(event->surface);
    if (!target || !target->mapped) {
        if (event->token) {
            wlr_xdg_activation_token_v1_destroy(event->token);
        }
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

    if (event->token) {
        wlr_xdg_activation_token_v1_destroy(event->token);
    }
}
static bool popup_unconstrain(popup* p) {
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

static void popup_commit(popup* p) {
    // unconstrain once, on the first commit after the surface is initialized
    if (!p->unconstrained) {
        p->unconstrained = popup_unconstrain(p);
    }
}

static void popup_reposition(popup* p) {
    popup_unconstrain(p);
}

static void popup_destroy(popup* p) {
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
    p->commit.connect(&xdg_popup->base->surface->events.commit, [p](void*) { popup_commit(p); });
    p->reposition.connect(&xdg_popup->events.reposition, [p](void*) { popup_reposition(p); });
    p->destroy.connect(&xdg_popup->events.destroy, [p](void*) { popup_destroy(p); });
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
            view* prev_view = static_cast<view*>(xdg->toplevel->base->data);

            // unfocus it
            if (prev_view) {
                prev_view->deco_set_focus(false);
            }
        }
    }

    // activate new view
    wlr_scene_node_raise_to_top(&scene_tree->node);
    wl_list_remove(&link);
    wl_list_insert(&s->views, &link);
    wlr_xdg_toplevel_set_activated(toplevel, true);
    deco_set_focus(true);

    // notify the seat that the keyboard now focuses this view
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }

    // a window now owns keyboard focus, so no layer surface does
    s->focused_layer = nullptr;

    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        if (out->taskbar) {
            out->taskbar->raise();
            out->taskbar->refresh();
        }
        // keep TOP and OVERLAY layer surfaces above taskbar
        for (int i = ZWLR_LAYER_SHELL_V1_LAYER_TOP; i <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; i++) {
            if (out->layer_trees[i]) {
                wlr_scene_node_raise_to_top(&out->layer_trees[i]->node);
            }
        }
    }
}
