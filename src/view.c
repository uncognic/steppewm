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

#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "deco.h"
#include "input.h"
#include "output.h"
#include "server.h"
#include "taskbar.h"
#include "view.h"

static bool view_can_configure(struct steppewm_view *view) {
    return view->toplevel->base->initialized;
}

// remove wl_listener listener
static void remove_listener(struct wl_listener *listener) {
    if (listener->link.next && listener->link.next != &listener->link) {
        wl_list_remove(&listener->link);
    }
    wl_list_init(&listener->link);
}

void view_focus_next(struct steppewm_server *server, struct steppewm_view *skip) {
    struct steppewm_view *next = nullptr;
    struct steppewm_view *view;

    // loop through views until we find one that isn't minimzed
    wl_list_for_each(view, &server->views, link) {
        if (view != skip && view->mapped && !view->minimized) {
            next = view;
            break;
        }
    }

    // focus the next window, otherwise clear focus
    if (next) {
        view_focus(next, next->toplevel->base->surface);
    } else {
        wlr_seat_keyboard_notify_clear_focus(server->seat);
        struct steppewm_output *out;
        // update for each output's taskbar
        wl_list_for_each(out, &server->outputs, link) {
            if (out->taskbar) {
                taskbar_refresh(out->taskbar);
            }
        }
    }
}

// minimize a view and hide it from the scene
void view_minimize(struct steppewm_view *view, bool minimized) {
    view->minimized = minimized;
    wlr_scene_node_set_enabled(&view->scene_tree->node, !minimized);
    struct steppewm_output *out;
    // update for each output's taskbar
    wl_list_for_each(out, &view->server->outputs, link) {
        if (out->taskbar) {
            taskbar_refresh(out->taskbar);
        }
    }
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
    struct steppewm_view *view = data;
    view->initial_configure_idle = nullptr;

    if (!view_can_configure(view)) {
        return;
    }

    view_apply_pending_deco(view);
    wlr_xdg_toplevel_set_size(view->toplevel, 0, 0);
}

// focus a new window
static void view_map(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_view *view = wl_container_of(listener, view, map);

    // set properties
    view->mapped = true;
    wlr_scene_node_set_enabled(&view->scene_tree->node, true);
    wl_list_insert(&view->server->views, &view->link);

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
static void view_unmap(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_view *view = wl_container_of(listener, view, unmap);
    if (view->server->grabbed_view == view) {
        view->server->grabbed_view = nullptr;
        view->server->cursor_mode = STEPPEWM_CURSOR_PASSTHROUGH;
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
static void view_commit(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_view *view = wl_container_of(listener, view, commit);
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
static void view_destroy(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_view *view = wl_container_of(listener, view, destroy);
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
    view->toplevel->base->data = NULL;
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->request_minimize.link);
    remove_listener(&view->request_deco_mode);
    remove_listener(&view->destroy_deco);
    deco_destroy(view);
    wlr_scene_node_destroy(&view->scene_tree->node);
    free(view);
}

// when the client wants to move or resize a window
static void view_request_move(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_view *view = wl_container_of(listener, view, request_move);
    cursor_begin_interactive(view, STEPPEWM_CURSOR_MOVE, 0);
}

static void view_request_resize(struct wl_listener *listener, void *data) {
    struct steppewm_view *view = wl_container_of(listener, view, request_resize);
    struct wlr_xdg_toplevel_resize_event *event = data;
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
static void view_request_maximize(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_view *view = wl_container_of(listener, view, request_maximize);
    view_apply_state(view, view->toplevel->requested.maximized, view->fullscreen);
}

// fullscreen a view
static void view_request_fullscreen(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_view *view = wl_container_of(listener, view, request_fullscreen);
    view_apply_state(view, view->maximized, view->toplevel->requested.fullscreen);
}

// minimize a view, handing focus to the next visible window
static void view_on_request_minimize(struct wl_listener *listener, void *data) {
    (void) data;
    struct steppewm_view *view = wl_container_of(listener, view, request_minimize);
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

// create a new view
void view_new(struct wl_listener *listener, void *data) {
    // get objects
    struct steppewm_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;
    struct steppewm_view *view = calloc(1, sizeof(*view));

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

    wl_list_init(&view->request_deco_mode.link);
    wl_list_init(&view->destroy_deco.link);

    // set everything
    view->map.notify = view_map;
    view->unmap.notify = view_unmap;
    view->commit.notify = view_commit;
    view->destroy.notify = view_destroy;
    view->request_move.notify = view_request_move;
    view->request_resize.notify = view_request_resize;
    view->request_maximize.notify = view_request_maximize;
    view->request_fullscreen.notify = view_request_fullscreen;
    view->request_minimize.notify = view_on_request_minimize;

    wl_signal_add(&toplevel->base->surface->events.map, &view->map);
    wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);
    wl_signal_add(&toplevel->base->surface->events.commit, &view->commit);
    wl_signal_add(&toplevel->events.destroy, &view->destroy);
    wl_signal_add(&toplevel->events.request_move, &view->request_move);
    wl_signal_add(&toplevel->events.request_resize, &view->request_resize);
    wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);
    wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);
    wl_signal_add(&toplevel->events.request_minimize, &view->request_minimize);
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
    return tree->node.data;
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
            struct steppewm_view *prev_view = xdg->toplevel->base->data;

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

    struct steppewm_output *out;
    wl_list_for_each(out, &server->outputs, link) {
        if (out->taskbar) {
            wlr_scene_node_raise_to_top(&out->taskbar->tree->node);
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
