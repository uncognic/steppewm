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

#include <linux/input-event-codes.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "deco.h"
#include "input.h"
#include "server.h"
#include "view.h"

//// keyboard

// this is where lua keybinds will be put but for now we dont have lua yet
static bool handle_keybinding(struct steppewm_server *server, xkb_keysym_t sym) {
    switch (sym) {
        case XKB_KEY_Escape:
            wl_display_terminate(server->display);
            return true;
        case XKB_KEY_Tab: {
            // bail if no views
            if (wl_list_empty(&server->views)) {
                return true;
            }
            struct steppewm_view *next = wl_container_of(server->views.prev, next, link);
            view_focus(next, next->toplevel->base->surface);
            return true;
        }
        case XKB_KEY_m: {
            // minimize focused window
            struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
            if (!focused) {
                return true;
            }
            struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(focused);
            if (!xdg || xdg->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
                return true;
            }
            struct steppewm_view *view = xdg->toplevel->base->data;
            view_minimize(view, true);
            struct steppewm_view *next = NULL;
            struct steppewm_view *v;
            wl_list_for_each(v, &server->views, link) {
                if (v != view && !v->minimized) {
                    next = v;
                    break;
                }
            }
            if (next) {
                view_focus(next, next->toplevel->base->surface);
            } else {
                wlr_seat_keyboard_notify_clear_focus(server->seat);
            }
            return true;
        }
        default:
            return false;
    }
}

// called when keyboard modifiers change
static void keyboard_modifiers(struct wl_listener *listener, void *data) {
    // get the steppewm_keyboard from the listener
    struct steppewm_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);

    // focus keyboard
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);

    // send modifier
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

// handles key presses and releases
static void keyboard_key(struct wl_listener *listener, void *data) {
    // get the steppewm_keyboard from the listener
    struct steppewm_keyboard *keyboard = wl_container_of(listener, keyboard, key);

    // get the steppewm_server from the keyboard
    struct steppewm_server *server = keyboard->server;

    // get the key event
    struct wlr_keyboard_key_event *event = data;

    // get the seat from the server
    struct wlr_seat *seat = server->seat;

    // convert libinput keycode to xkbcommmon keycode
    uint32_t keycode = event->keycode + 8;

    // make keycode into keysim
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    // check for keybinds
    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
    if ((modifiers & WLR_MODIFIER_ALT) && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            handled = handle_keybinding(server, syms[i]) || handled;
        }
    }

    // if the key event wasnt a keybind, pass it to the focused client
    if (!handled) {
        wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
    }
}

// destroy keyboard
static void keyboard_destroy(struct wl_listener *listener, void *data) {
    struct steppewm_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

// add keyboard
static void keyboard_new(struct steppewm_server *server, struct wlr_input_device *device) {
    // create keyboard
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    // create steppewm_keyboard
    struct steppewm_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    keyboard->server = server;
    keyboard->wlr_keyboard = wlr_keyboard;

    // set up xkb keymap with en-US
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

    // set the keymap
    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

    // set up listeners
    keyboard->modifiers.notify = keyboard_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = keyboard_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
    keyboard->destroy.notify = keyboard_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(server->seat, wlr_keyboard);

    wl_list_insert(&server->keyboards, &keyboard->link);
}

// add pointer
static void pointer_new(struct steppewm_server *server, struct wlr_input_device *device) {
    wlr_cursor_attach_input_device(server->cursor, device);
}

// create new input
void input_new(struct wl_listener *listener, void *data) {
    struct steppewm_server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;

    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            keyboard_new(server, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            pointer_new(server, device);
            break;
        default:
            break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat, caps);
}

//// cursor
// initiate move or resize operation for window
void cursor_begin_interactive(struct steppewm_view *view, enum steppewm_cursor_mode mode,
                              uint32_t edges) {
    // get objects
    struct steppewm_server *server = view->server;
    struct wlr_scene_node *node = &view->scene_tree->node;

    server->grabbed_view = view;
    server->cursor_mode = mode;

    // calculate grab offsets
    if (mode == STEPPEWM_CURSOR_MOVE) {
        // store offset from cur to top left corner of window
        // maintains relative grab point during dragging
        server->grab_x = server->cursor->x - node->x;
        server->grab_y = server->cursor->y - node->y;
    } else {
        struct wlr_box *geo = &view->toplevel->base->geometry;
        // offset by ssd
        int ox = view->deco_mode == STEPPEWM_DECO_SERVER ? STEPPEWM_BORDER_W : 0;
        int oy = view->deco_mode == STEPPEWM_DECO_SERVER ? STEPPEWM_TITLE_H : 0;
        // calculate surface position in space
        int sx = node->x + ox + geo->x;
        int sy = node->y + oy + geo->y;
        // calculate which corner / edge the resize started from
        double border_x = sx + ((edges & WLR_EDGE_RIGHT) ? geo->width : 0);
        double border_y = sy + ((edges & WLR_EDGE_BOTTOM) ? geo->height : 0);
        // store offset from cursor to border
        server->grab_x = server->cursor->x - border_x;
        server->grab_y = server->cursor->y - border_y;

        // store original geometry
        server->grab_geobox = (struct wlr_box){sx, sy, geo->width, geo->height};
        server->resize_edges = edges;
    }
}

// set cursor position
static void process_cursor_move(struct steppewm_server *server) {
    struct steppewm_view *view = server->grabbed_view;
    wlr_scene_node_set_position(&view->scene_tree->node, server->cursor->x - server->grab_x,
                                server->cursor->y - server->grab_y);
}

// resize window with cursor movement
static void process_cursor_resize(struct steppewm_server *server) {
    struct steppewm_view *view = server->grabbed_view;
    double border_x = server->cursor->x - server->grab_x;
    double border_y = server->cursor->y - server->grab_y;
    int new_left = server->grab_geobox.x;
    int new_right = server->grab_geobox.x + server->grab_geobox.width;
    int new_top = server->grab_geobox.y;
    int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

    if (server->resize_edges & WLR_EDGE_TOP) {
        new_top = border_y;
        if (new_top >= new_bottom) {
            new_top = new_bottom - 1;
        }
    } else if (server->resize_edges & WLR_EDGE_BOTTOM) {
        new_bottom = border_y;
        if (new_bottom <= new_top) {
            new_bottom = new_top + 1;
        }
    }
    if (server->resize_edges & WLR_EDGE_LEFT) {
        new_left = border_x;
        if (new_left >= new_right) {
            new_left = new_right - 1;
        }
    } else if (server->resize_edges & WLR_EDGE_RIGHT) {
        new_right = border_x;
        if (new_right <= new_left) {
            new_right = new_left + 1;
        }
    }

    struct wlr_box *geo = &view->toplevel->base->geometry;
    int ox = view->deco_mode == STEPPEWM_DECO_SERVER ? STEPPEWM_BORDER_W : 0;
    int oy = view->deco_mode == STEPPEWM_DECO_SERVER ? STEPPEWM_TITLE_H : 0;
    wlr_scene_node_set_position(&view->scene_tree->node, new_left - ox - geo->x,
                                new_top - oy - geo->y);
    wlr_xdg_toplevel_set_size(view->toplevel, new_right - new_left, new_bottom - new_top);
}

// called on cursor motion events
static void process_cursor_motion(struct steppewm_server *server, uint32_t time_msec) {
    if (server->cursor_mode == STEPPEWM_CURSOR_MOVE) {
        process_cursor_move(server);
        return;
    }
    if (server->cursor_mode == STEPPEWM_CURSOR_RESIZE) {
        process_cursor_resize(server);
        return;
    }

    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct steppewm_view *view =
        view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (!view) {
        const char *cursor_name = "default";
        double tmp_sx, tmp_sy;
        struct wlr_scene_node *hnode = wlr_scene_node_at(
            &server->scene->tree.node, server->cursor->x, server->cursor->y, &tmp_sx, &tmp_sy);
        if (hnode && hnode->type == WLR_SCENE_NODE_RECT) {
            struct wlr_scene_tree *tree = hnode->parent;
            while (tree && !tree->node.data) {
                tree = tree->node.parent;
            }
            if (tree) {
                struct steppewm_view *dv = tree->node.data;
                // change cursor depending on where its resizing
                if (dv->deco_mode == STEPPEWM_DECO_SERVER) {
                    if (hnode == &dv->deco.corner_bl->node) {
                        cursor_name = "sw-resize";
                    } else if (hnode == &dv->deco.corner_br->node) {
                        cursor_name = "se-resize";
                    } else if (hnode == &dv->deco.border_left->node) {
                        cursor_name = "w-resize";
                    } else if (hnode == &dv->deco.border_right->node) {
                        cursor_name = "e-resize";
                    } else if (hnode == &dv->deco.border_bottom->node) {
                        cursor_name = "s-resize";
                    }
                }
            }
        }
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, cursor_name);
    }

    struct wlr_seat *seat = server->seat;
    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time_msec, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(seat);
    }
}

// handle cursor motion events
void cursor_motion(struct wl_listener *listener, void *data) {
    // get objects
    struct steppewm_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;

    // move the cursor
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

// handle absolute cursor motion events
void cursor_motion_absolute(struct wl_listener *listener, void *data) {
    // get objects
    struct steppewm_server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;

    // do the absolute move
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
}

// handle cursor button events
void cursor_button(struct wl_listener *listener, void *data) {
    // get objects
    struct steppewm_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;

    // notify the seat of the event
    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);

    // if the button was released, end any operation
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        server->cursor_mode = STEPPEWM_CURSOR_PASSTHROUGH;
        server->grabbed_view = NULL;
        return;
    }

    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct steppewm_view *view =
        view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    // alt drag for compositor initiated move/resize
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    if ((mods & WLR_MODIFIER_ALT) && view) {
        view_focus(view, surface);
        // left click for move
        if (event->button == BTN_LEFT) {
            cursor_begin_interactive(view, STEPPEWM_CURSOR_MOVE, 0);
            return;
        }

        // right click for resize
        if (event->button == BTN_RIGHT) {
            struct wlr_box *geo = &view->toplevel->base->geometry;
            struct wlr_scene_node *node = &view->scene_tree->node;
            uint32_t edges =
                (server->cursor->x < node->x + geo->x + geo->width / 2.0 ? WLR_EDGE_LEFT
                                                                         : WLR_EDGE_RIGHT) |
                (server->cursor->y < node->y + geo->y + geo->height / 2.0 ? WLR_EDGE_TOP
                                                                          : WLR_EDGE_BOTTOM);
            cursor_begin_interactive(view, STEPPEWM_CURSOR_RESIZE, edges);
            return;
        }
    }

    if (view) {
        view_focus(view, surface);
        return;
    }

    // no view was clicked, check if a titlebar or border was clicked for move/resize
    double tmp_sx, tmp_sy;
    struct wlr_scene_node *hnode = wlr_scene_node_at(&server->scene->tree.node, server->cursor->x,
                                                     server->cursor->y, &tmp_sx, &tmp_sy);
    if (hnode && hnode->type == WLR_SCENE_NODE_RECT) {
        struct wlr_scene_tree *tree = hnode->parent;
        while (tree && !tree->node.data) {
            tree = tree->node.parent;
        }

        if (tree) {
            struct steppewm_view *dv = tree->node.data;
            view_focus(dv, dv->toplevel->base->surface);
            if (dv->deco_mode == STEPPEWM_DECO_SERVER) {
                if (hnode == &dv->deco.titlebar->node) {
                    cursor_begin_interactive(dv, STEPPEWM_CURSOR_MOVE, 0);
                } else if (hnode == &dv->deco.corner_bl->node) {
                    cursor_begin_interactive(dv, STEPPEWM_CURSOR_RESIZE,
                                             WLR_EDGE_BOTTOM | WLR_EDGE_LEFT);
                } else if (hnode == &dv->deco.corner_br->node) {
                    cursor_begin_interactive(dv, STEPPEWM_CURSOR_RESIZE,
                                             WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
                } else if (hnode == &dv->deco.border_left->node) {
                    cursor_begin_interactive(dv, STEPPEWM_CURSOR_RESIZE, WLR_EDGE_LEFT);
                } else if (hnode == &dv->deco.border_right->node) {
                    cursor_begin_interactive(dv, STEPPEWM_CURSOR_RESIZE, WLR_EDGE_RIGHT);
                } else if (hnode == &dv->deco.border_bottom->node) {
                    cursor_begin_interactive(dv, STEPPEWM_CURSOR_RESIZE, WLR_EDGE_BOTTOM);
                }
            }
        }
    }
}

// handle cursor scroll wheel / axis events
void cursor_axis(struct wl_listener *listener, void *data) {
    // get objects
    struct steppewm_server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;

    // forward scroll event to the seat
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta,
                                 event->delta_discrete, event->source, event->relative_direction);
}

// handle cursor frame events
void cursor_frame(struct wl_listener *listener, void *data) {
    // get objects
    struct steppewm_server *server = wl_container_of(listener, server, cursor_frame);

    // notify seat
    wlr_seat_pointer_notify_frame(server->seat);
}

//// seat requests

// handle cursor image change requests from clients
void request_set_cursor(struct wl_listener *listener, void *data) {
    // get objects
    struct steppewm_server *server = wl_container_of(listener, server, request_set_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;

    // only allow focused client to change cursor
    struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
    if (focused_client == event->seat_client) {
        // update cursor surface with new image and hotspot
        wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

// handle clipboard / selection change requests from clients
void request_set_selection(struct wl_listener *listener, void *data) {
    // get objects
    struct steppewm_server *server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;

    // update seat selection (clipboard)
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

// handle primary selection requests from clients
void request_set_primary_selection(struct wl_listener *listener, void *data) {
    struct steppewm_server *server =
        wl_container_of(listener, server, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}
