#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "deco.h"
#include "input.h"
#include "server.h"
#include "view.h"

static bool view_can_configure(struct steppewm_view *view) {
    return view->toplevel->base->initialized;
}

// minimize a view and hide it from the scene
void view_minimize(struct steppewm_view *view, bool minimized) {
    view->minimized = minimized;
    wlr_scene_node_set_enabled(&view->scene_tree->node, !minimized);
}

// apply ssd deco
static void view_apply_pending_deco(struct steppewm_view *view) {
    if (!view->pending_deco || !view_can_configure(view)) {
        return;
    }

    wlr_xdg_toplevel_decoration_v1_set_mode(view->pending_deco,
                                            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    view->deco_mode = STEPPEWM_DECO_SERVER;
    wlr_scene_node_set_position(&view->xdg_tree->node, STEPPEWM_BORDER_W, STEPPEWM_TITLE_H);
    deco_create(view);
    view->pending_deco = NULL;
}

// move
static void view_initial_configure(void *data) {
    struct steppewm_view *view = data;
    view->initial_configure_idle = NULL;

    if (!view_can_configure(view)) {
        return;
    }

    view_apply_pending_deco(view);
    wlr_xdg_toplevel_set_size(view->toplevel, 0, 0);
}

// focus a new window
static void view_map(struct wl_listener *listener, void *data) {
    struct steppewm_view *view = wl_container_of(listener, view, map);
    wl_list_insert(&view->server->views, &view->link);
    view_focus(view, view->toplevel->base->surface);
}

// remove window (view)
static void view_unmap(struct wl_listener *listener, void *data) {
    struct steppewm_view *view = wl_container_of(listener, view, unmap);
    if (view->server->grabbed_view == view) {
        view->server->grabbed_view = NULL;
        view->server->cursor_mode = STEPPEWM_CURSOR_PASSTHROUGH;
    }
    wl_list_remove(&view->link);
}

// update/render window
static void view_commit(struct wl_listener *listener, void *data) {
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
    struct steppewm_view *view = wl_container_of(listener, view, destroy);
    if (view->initial_configure_idle) {
        wl_event_source_remove(view->initial_configure_idle);
    }
    deco_destroy(view);
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->request_minimize.link);
    wl_list_remove(&view->request_deco_mode.link);
    free(view);
}

// when the client wants to move or resize a window
static void view_request_move(struct wl_listener *listener, void *data) {
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
            view->saved_geo = (struct wlr_box){
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

        // resize window to fill output minus the titlebars and borders
        int ox = view->deco_mode == STEPPEWM_DECO_SERVER ? STEPPEWM_BORDER_W : 0;
        int oy = view->deco_mode == STEPPEWM_DECO_SERVER ? STEPPEWM_TITLE_H : 0;
        wlr_xdg_toplevel_set_size(view->toplevel, out_box.width - 2 * ox, out_box.height - oy - ox);

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
    struct steppewm_view *view = wl_container_of(listener, view, request_maximize);
    view_apply_state(view, view->toplevel->requested.maximized, view->fullscreen);
}

// fullscreen a view
static void view_request_fullscreen(struct wl_listener *listener, void *data) {
    struct steppewm_view *view = wl_container_of(listener, view, request_fullscreen);
    view_apply_state(view, view->maximized, view->toplevel->requested.fullscreen);
}

// minimize a view, handing focus to the next visible window
static void view_on_request_minimize(struct wl_listener *listener, void *data) {
    struct steppewm_view *view = wl_container_of(listener, view, request_minimize);
    if (!view->toplevel->requested.minimized) {
        return;
    }
    view_minimize(view, true);
    struct steppewm_view *next = NULL;
    struct steppewm_view *v;
    wl_list_for_each(v, &view->server->views, link) {
        if (v != view && !v->minimized) {
            next = v;
            break;
        }
    }
    if (next) {
        view_focus(next, next->toplevel->base->surface);
    } else {
        wlr_seat_keyboard_notify_clear_focus(view->server->seat);
    }
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
        return NULL;
    }

    // get the wayland surface from the node
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }
    *surface = scene_surface->surface;

    // find the view
    struct wlr_scene_tree *tree = node->parent;
    while (tree && !tree->node.data) {
        tree = tree->node.parent;
    }

    // return null if no data
    return tree ? tree->node.data : NULL;
}

// focus a view
void view_focus(struct steppewm_view *view, struct wlr_surface *surface) {
    // if no view was providewd
    if (!view) {
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
            deco_set_focus(prev_view, false);
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
}
