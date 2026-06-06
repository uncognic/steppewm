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

#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#include <libinput.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "deco.hxx"
#include "input.hxx"
#include "output.hxx"
#include "server.hxx"
#include "switcher.hxx"
#include "taskbar.hxx"
#include "view.hxx"

using namespace steppewm;

// keyboard
static void spawn(const char* cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() > 0) {
            _exit(0);
        }
        execl("/bin/sh", "sh", "-c", cmd, static_cast<char*>(nullptr));
        _exit(1);
    }
    if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }
}

// redraw every output's taskbar
static void refresh_taskbars(server* s) {
    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        if (out->taskbar) {
            out->taskbar->refresh();
        }
    }
}

static view* focused_view(server* s) {
    struct wlr_surface* surf = s->seat->keyboard_state.focused_surface;
    if (!surf) {
        return nullptr;
    }
    struct wlr_xdg_surface* xdg = wlr_xdg_surface_try_from_wlr_surface(surf);
    if (!xdg || xdg->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return nullptr;
    }
    return static_cast<view*>(xdg->toplevel->base->data);
}

static void dispatch_action(server* s, const char* action, const char* arg, uint32_t mods) {
    if (strcmp(action, "quit") == 0) {
        wl_display_terminate(s->display);
    } else if (strcmp(action, "focus_next") == 0) {
        switcher::cycle(s, mods, false);
    } else if (strcmp(action, "focus_prev") == 0) {
        switcher::cycle(s, mods, true);
    } else if (strcmp(action, "spawn") == 0) {
        if (arg && arg[0]) {
            spawn(arg);
        }
    } else if (strcmp(action, "reload") == 0) {
        config_reload(s);
    } else if (strcmp(action, "workspace") == 0) {
        if (arg && arg[0]) {
            workspace_switch(s, atoi(arg) - 1);
        }
    } else if (strcmp(action, "move_to_workspace") == 0) {
        view* v = focused_view(s);
        if (v && arg && arg[0]) {
            v->move_to_workspace(atoi(arg) - 1);
        }
    } else {
        view* v = focused_view(s);
        if (!v) {
            return;
        }
        if (strcmp(action, "minimize") == 0) {
            v->minimize(true);
            view::focus_next(s, v);
        } else if (strcmp(action, "maximize") == 0) {
            v->toggle_maximize();
        } else if (strcmp(action, "close") == 0) {
            wlr_xdg_toplevel_send_close(v->toplevel);
        }
    }
}

static bool handle_keybinding(server* s, uint32_t mods, xkb_keysym_t sym) {
    // make keysim lowercase so bindings still match
    xkb_keysym_t lower = xkb_keysym_to_lower(sym);
    for (int i = 0; i < s->cfg.nbinds; i++) {
        keybind* b = &s->cfg.binds[i];
        if (b->modifiers == mods && b->sym == lower) {
            dispatch_action(s, b->action, b->arg, b->modifiers);
            return true;
        }
    }
    return false;
}

// called when keyboard modifiers change
static void keyboard_modifiers(struct wl_listener* listener, void* data) {
    (void) data;
    // get the keyboard from the listener
    keyboard* kbd = wl_container_of(listener, kbd, modifiers);

    // focus keyboard
    wlr_seat_set_keyboard(kbd->srv->seat, kbd->wlr_keyboard);

    // send modifier
    wlr_seat_keyboard_notify_modifiers(kbd->srv->seat, &kbd->wlr_keyboard->modifiers);

    switcher::handle_modifiers(kbd->srv, wlr_keyboard_get_modifiers(kbd->wlr_keyboard));

    uint32_t group = kbd->wlr_keyboard->modifiers.group;
    if (group != kbd->srv->layout_group) {
        kbd->srv->layout_group = group;
        refresh_taskbars(kbd->srv);
    }
}

// handles key presses and releases
static void keyboard_key(struct wl_listener* listener, void* data) {
    // get the keyboard from the listener
    keyboard* kbd = wl_container_of(listener, kbd, key);

    // get the server from the keyboard
    server* s = kbd->srv;

    // get the key event
    struct wlr_keyboard_key_event* event = static_cast<struct wlr_keyboard_key_event*>(data);

    // get the seat from the server
    struct wlr_seat* seat = s->seat;

    // convert libinput keycode to xkbcommmon keycode
    uint32_t keycode = event->keycode + 8;

    // make keycode into keysim
    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(kbd->wlr_keyboard->xkb_state, keycode, &syms);

    // check for keybinds, only when a modifier is held
    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(kbd->wlr_keyboard);
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            if (syms[i] >= XKB_KEY_XF86Switch_VT_1 && syms[i] <= XKB_KEY_XF86Switch_VT_12) {
                if (s->session) {
                    wlr_session_change_vt(s->session, syms[i] - XKB_KEY_XF86Switch_VT_1 + 1);
                }
                handled = true;
            }
        }
    }
    // escape cancels an alt tab without changing focus
    if (!handled && event->state == WL_KEYBOARD_KEY_STATE_PRESSED && s->sw) {
        for (int i = 0; i < nsyms; i++) {
            if (syms[i] == XKB_KEY_Escape) {
                switcher::cancel(s);
                handled = true;
            }
        }
    }
    if (!handled && modifiers && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            handled = handle_keybinding(s, modifiers, syms[i]) || handled;
        }

        if (!handled) {
            struct xkb_keymap* keymap = xkb_state_get_keymap(kbd->wlr_keyboard->xkb_state);
            xkb_layout_index_t layout =
                xkb_state_key_get_layout(kbd->wlr_keyboard->xkb_state, keycode);
            const xkb_keysym_t* raw_syms;
            int raw_nsyms = xkb_keymap_key_get_syms_by_level(keymap, keycode, layout, 0, &raw_syms);
            for (int i = 0; i < raw_nsyms; i++) {
                handled = handle_keybinding(s, modifiers, raw_syms[i]) || handled;
            }
        }
    }

    // if the key event wasnt a keybind, pass it to the focused client
    if (!handled) {
        wlr_seat_set_keyboard(seat, kbd->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
    }
}

// destroy keyboard
static void keyboard_destroy(struct wl_listener* listener, void* data) {
    (void) data;
    keyboard* kbd = wl_container_of(listener, kbd, destroy);
    wl_list_remove(&kbd->modifiers.link);
    wl_list_remove(&kbd->key.link);
    wl_list_remove(&kbd->destroy.link);
    wl_list_remove(&kbd->link);
    delete kbd;
}

static void keyboard_apply_config(server* s, struct wlr_keyboard* wlr_keyboard) {
    config* cfg = &s->cfg;

    // empty strings stay null so xkbcommon uses its defaults
    struct xkb_rule_names rules = {};
    rules.layout = cfg->xkb_layout[0] ? cfg->xkb_layout : nullptr;
    rules.variant = cfg->xkb_variant[0] ? cfg->xkb_variant : nullptr;
    rules.options = cfg->xkb_options[0] ? cfg->xkb_options : nullptr;

    struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap* keymap =
        xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap) {
        wlr_keyboard_set_keymap(wlr_keyboard, keymap);
        xkb_keymap_unref(keymap);
    } else {
        wlr_log(WLR_ERROR, "failed to compile keymap for layout '%s'", cfg->xkb_layout);
    }
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(wlr_keyboard, cfg->repeat_rate, cfg->repeat_delay);
}

// apply the configured libinput settings to a pointer
static void pointer_apply_config(server* s, struct wlr_input_device* device) {
    if (!wlr_input_device_is_libinput(device)) {
        return;
    }
    struct libinput_device* dev = wlr_libinput_get_device_handle(device);
    if (!dev) {
        return;
    }
    config* cfg = &s->cfg;

    // tap to click only on devices that support tapping
    if (libinput_device_config_tap_get_finger_count(dev) > 0) {
        libinput_device_config_tap_set_enabled(
            dev, cfg->tap_to_click ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
    }

    // natural scrolling
    if (libinput_device_config_scroll_has_natural_scroll(dev)) {
        libinput_device_config_scroll_set_natural_scroll_enabled(dev, cfg->natural_scroll);
    }

    // pointer acceleration speed and profile
    if (libinput_device_config_accel_is_available(dev)) {
        double speed = cfg->pointer_accel;
        if (speed < -1.0) {
            speed = -1.0;
        }
        if (speed > 1.0) {
            speed = 1.0;
        }
        libinput_device_config_accel_set_speed(dev, speed);

        if (strcmp(cfg->accel_profile, "flat") == 0) {
            libinput_device_config_accel_set_profile(dev, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
        } else if (strcmp(cfg->accel_profile, "adaptive") == 0) {
            libinput_device_config_accel_set_profile(dev, LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE);
        }
    }
}

// add keyboard
static void keyboard_new(server* s, struct wlr_input_device* device) {
    // create keyboard
    struct wlr_keyboard* wlr_keyboard = wlr_keyboard_from_input_device(device);

    // create keyboard
    auto* kbd = new keyboard();
    kbd->srv = s;
    kbd->wlr_keyboard = wlr_keyboard;

    keyboard_apply_config(s, wlr_keyboard);

    // set up listeners
    kbd->modifiers.notify = keyboard_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &kbd->modifiers);
    kbd->key.notify = keyboard_key;
    wl_signal_add(&wlr_keyboard->events.key, &kbd->key);
    kbd->destroy.notify = keyboard_destroy;
    wl_signal_add(&device->events.destroy, &kbd->destroy);

    wlr_seat_set_keyboard(s->seat, wlr_keyboard);

    wl_list_insert(&s->keyboards, &kbd->link);

    // show the layout indicator once a keyboard is present
    refresh_taskbars(s);
}

// destroy pointer
static void pointer_destroy(struct wl_listener* listener, void* data) {
    (void) data;
    pointer* ptr = wl_container_of(listener, ptr, destroy);
    wl_list_remove(&ptr->destroy.link);
    wl_list_remove(&ptr->link);
    delete ptr;
}

// add pointer
static void pointer_new(server* s, struct wlr_input_device* device) {
    wlr_cursor_attach_input_device(s->cursor, device);
    pointer_apply_config(s, device);

    // track the device so settings can be re-applied on config reload
    auto* ptr = new pointer();
    ptr->srv = s;
    ptr->device = device;
    ptr->destroy.notify = pointer_destroy;
    wl_signal_add(&device->events.destroy, &ptr->destroy);
    wl_list_insert(&s->pointers, &ptr->link);
}

// create new input
void steppewm::input_new(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, new_input);
    struct wlr_input_device* device = static_cast<struct wlr_input_device*>(data);

    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            keyboard_new(s, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            pointer_new(s, device);
            break;
        default:
            break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&s->keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(s->seat, caps);
}

// reapply input config to every existing device, called on config reload
void steppewm::input_reconfigure(server* s) {
    keyboard* kbd;
    wl_list_for_each(kbd, &s->keyboards, link) {
        keyboard_apply_config(s, kbd->wlr_keyboard);
    }

    pointer* ptr;
    wl_list_for_each(ptr, &s->pointers, link) {
        pointer_apply_config(s, ptr->device);
    }
}

//// cursor
// initiate move or resize operation for window
void steppewm::cursor_begin_interactive(view* v, cursor_mode mode, uint32_t edges) {
    // get objects
    server* s = v->srv;
    struct wlr_scene_node* node = &v->scene_tree->node;

    s->grabbed_view = v;
    s->grab_mode = mode;
    s->grab_restore_pending = false;

    // calculate grab offsets
    if (mode == cursor_mode::MOVE) {
        if (v->maximized || v->fullscreen) {
            s->grab_restore_pending = true;
            s->grab_start_x = s->cursor->x;
            s->grab_start_y = s->cursor->y;
        }
        s->grab_x = s->cursor->x - node->x;
        s->grab_y = s->cursor->y - node->y;
    } else {
        struct wlr_box* geo = &v->toplevel->base->geometry;
        int ox = v->decoration_mode == deco_mode::SERVER ? s->cfg.border_w : 0;
        int oy = v->decoration_mode == deco_mode::SERVER ? s->cfg.title_h : 0;
        // calculate surface position in space
        int sx = node->x + ox + geo->x;
        int sy = node->y + oy + geo->y;
        // calculate which corner / edge the resize started from
        double border_x = sx + (edges & WLR_EDGE_RIGHT ? geo->width : 0);
        double border_y = sy + (edges & WLR_EDGE_BOTTOM ? geo->height : 0);
        // store offset from cursor to border
        s->grab_x = s->cursor->x - border_x;
        s->grab_y = s->cursor->y - border_y;

        // store original geometry
        s->grab_geobox = (struct wlr_box) {sx, sy, geo->width, geo->height};
        s->resize_edges = edges;
    }
}

// distance the pointer must travel before a maximized window starts dragging
#define DRAG_THRESHOLD 5.0

// set cursor position
static void process_cursor_move(server* s) {
    view* v = s->grabbed_view;

    // a grab on a maximized window waits for a real drag before restoring it
    if (s->grab_restore_pending) {
        double dx = s->cursor->x - s->grab_start_x;
        double dy = s->cursor->y - s->grab_start_y;

        // still just a click, leave the window maximized
        if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD) {
            return;
        }

        // crossed the threshold, restore under the cursor and recompute the offset
        v->unmaximize_to_cursor(s->cursor->x, s->cursor->y);
        s->grab_x = s->cursor->x - v->scene_tree->node.x;
        s->grab_y = s->cursor->y - v->scene_tree->node.y;
        s->grab_restore_pending = false;
    }

    wlr_scene_node_set_position(&v->scene_tree->node, (int) (s->cursor->x - s->grab_x),
                                (int) (s->cursor->y - s->grab_y));
}

// resize window with cursor movement
static void process_cursor_resize(server* s) {
    view* v = s->grabbed_view;
    double border_x = s->cursor->x - s->grab_x;
    double border_y = s->cursor->y - s->grab_y;
    int new_left = s->grab_geobox.x;
    int new_right = s->grab_geobox.x + s->grab_geobox.width;
    int new_top = s->grab_geobox.y;
    int new_bottom = s->grab_geobox.y + s->grab_geobox.height;

    if (s->resize_edges & WLR_EDGE_TOP) {
        new_top = (int) border_y;
        if (new_top >= new_bottom) {
            new_top = new_bottom - 1;
        }
    } else if (s->resize_edges & WLR_EDGE_BOTTOM) {
        new_bottom = (int) border_y;
        if (new_bottom <= new_top) {
            new_bottom = new_top + 1;
        }
    }
    if (s->resize_edges & WLR_EDGE_LEFT) {
        new_left = (int) border_x;
        if (new_left >= new_right) {
            new_left = new_right - 1;
        }
    } else if (s->resize_edges & WLR_EDGE_RIGHT) {
        new_right = (int) border_x;
        if (new_right <= new_left) {
            new_right = new_left + 1;
        }
    }

    struct wlr_box* geo = &v->toplevel->base->geometry;
    int ox = v->decoration_mode == deco_mode::SERVER ? s->cfg.border_w : 0;
    int oy = v->decoration_mode == deco_mode::SERVER ? s->cfg.title_h : 0;
    wlr_scene_node_set_position(&v->scene_tree->node, new_left - ox - geo->x,
                                new_top - oy - geo->y);
    wlr_xdg_toplevel_set_size(v->toplevel, new_right - new_left, new_bottom - new_top);
}

// called on cursor motion events
static void process_cursor_motion(server* s, uint32_t time_msec) {
    if (s->grab_mode == cursor_mode::MOVE) {
        process_cursor_move(s);
        return;
    }
    if (s->grab_mode == cursor_mode::RESIZE) {
        process_cursor_resize(s);
        return;
    }

    double sx, sy;
    struct wlr_surface* surface = nullptr;
    // view::at populates surfaces from hit test, so swaybg doesn't show up since it doesn't have an
    // input region, but slurp does
    view::at(s, s->cursor->x, s->cursor->y, &surface, &sx, &sy);

    struct wlr_seat* seat = s->seat;
    if (surface) {
        // deliver pointer events to whatever surface is under the cursor
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time_msec, sx, sy);
    } else {
        // empty area or decoration, pick the cursor ourselves and drop focus
        const char* cursor_name = "default";
        struct wlr_scene_node* hnode = nullptr;
        view* dview = deco_at(s, s->cursor->x, s->cursor->y, &hnode);
        const char* deco_cursor = dview ? dview->deco_cursor_name(hnode) : nullptr;
        if (deco_cursor) {
            cursor_name = deco_cursor;
        }
        wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, cursor_name);
        wlr_seat_pointer_clear_focus(seat);
    }
}

// handle cursor motion events
void steppewm::cursor_motion(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, cursor_motion);
    struct wlr_pointer_motion_event* event = static_cast<struct wlr_pointer_motion_event*>(data);

    // move the cursor
    wlr_cursor_move(s->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(s, event->time_msec);
}

// handle absolute cursor motion events
void steppewm::cursor_motion_absolute(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event* event =
        static_cast<struct wlr_pointer_motion_absolute_event*>(data);

    // do the absolute move
    wlr_cursor_warp_absolute(s->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(s, event->time_msec);
}

// handle cursor button events
void steppewm::cursor_button(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, cursor_button);
    struct wlr_pointer_button_event* event = static_cast<struct wlr_pointer_button_event*>(data);

    // notify the seat of the event
    wlr_seat_pointer_notify_button(s->seat, event->time_msec, event->button, event->state);

    // if the button was released, end any operation
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        s->grab_mode = cursor_mode::PASSTHROUGH;
        s->grabbed_view = nullptr;
        s->grab_restore_pending = false;
        return;
    }

    double sx, sy;
    struct wlr_surface* surface = nullptr;
    view* v = view::at(s, s->cursor->x, s->cursor->y, &surface, &sx, &sy);

    // alt drag for compositor initiated move/resize
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(s->seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    if (mods & WLR_MODIFIER_ALT && v) {
        v->focus(surface);
        // left click for move
        if (event->button == BTN_LEFT) {
            cursor_begin_interactive(v, cursor_mode::MOVE, 0);
            return;
        }

        // right click for resize
        if (event->button == BTN_RIGHT) {
            struct wlr_box* geo = &v->toplevel->base->geometry;
            struct wlr_scene_node* node = &v->scene_tree->node;
            uint32_t edges =
                (s->cursor->x < node->x + geo->x + geo->width / 2.0 ? WLR_EDGE_LEFT
                                                                    : WLR_EDGE_RIGHT) |
                (s->cursor->y < node->y + geo->y + geo->height / 2.0 ? WLR_EDGE_TOP
                                                                     : WLR_EDGE_BOTTOM);
            cursor_begin_interactive(v, cursor_mode::RESIZE, edges);
            return;
        }
    }

    if (v) {
        v->focus(surface);
        return;
    }

    // no view was clicked, check if a titlebar or border was clicked for move/resize
    struct wlr_scene_node* hnode = nullptr;
    view* dview = deco_at(s, s->cursor->x, s->cursor->y, &hnode);
    if (dview) {
        dview->focus(dview->toplevel->base->surface);
        dview->deco_handle_button(s, hnode, event->button);
        return;
    }

    // if a taskbar item was clicked
    if (event->button == BTN_LEFT) {
        output* out;
        wl_list_for_each(out, &s->outputs, link) {
            // prevent dereferencing NULL on outputs that don't have a taskbar
            if (!out->taskbar) {
                continue;
            }
            // if the workspace button was clicked
            int ws = out->taskbar->workspace_at(s->cursor->x, s->cursor->y);
            if (ws >= 0) {
                workspace_switch(s, ws);
                return;
            }
            view* tv = out->taskbar->view_at(s->cursor->x, s->cursor->y);
            if (tv) {
                tv->focus(tv->toplevel->base->surface);
                return;
            }
        }
    }
}

// handle cursor scroll wheel / axis events
void steppewm::cursor_axis(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, cursor_axis);
    struct wlr_pointer_axis_event* event = static_cast<struct wlr_pointer_axis_event*>(data);

    // forward scroll event to the seat
    wlr_seat_pointer_notify_axis(s->seat, event->time_msec, event->orientation, event->delta,
                                 event->delta_discrete, event->source, event->relative_direction);
}

// handle cursor frame events
void steppewm::cursor_frame(struct wl_listener* listener, void* data) {
    (void) data;
    // get objects
    server* s = wl_container_of(listener, s, cursor_frame);

    // notify seat
    wlr_seat_pointer_notify_frame(s->seat);
}

//// seat requests

// handle cursor image change requests from clients
void steppewm::request_set_cursor(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, request_set_cursor);
    const auto* event = static_cast<struct wlr_seat_pointer_request_set_cursor_event*>(data);

    // only allow focused client to change cursor
    struct wlr_seat_client* focused_client = s->seat->pointer_state.focused_client;
    if (focused_client == event->seat_client) {
        // update cursor surface with new image and hotspot
        wlr_cursor_set_surface(s->cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

// handle cursor shape requests from clients
void steppewm::request_set_shape(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, request_set_shape);
    auto* event = static_cast<struct wlr_cursor_shape_manager_v1_request_set_shape_event*>(data);

    // only allow focused client to change cursor
    struct wlr_seat_client* focused_client = s->seat->pointer_state.focused_client;
    if (focused_client == event->seat_client) {
        // load the named shape from the xcursor theme
        wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, wlr_cursor_shape_v1_name(event->shape));
    }
}

// handle clipboard / selection change requests from clients
void steppewm::request_set_selection(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, request_set_selection);
    auto* event = static_cast<struct wlr_seat_request_set_selection_event*>(data);

    // update seat selection (clipboard)
    wlr_seat_set_selection(s->seat, event->source, event->serial);
}

// handle primary selection requests from clients
void steppewm::request_set_primary_selection(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, request_set_primary_selection);
    auto* event = static_cast<struct wlr_seat_request_set_primary_selection_event*>(data);
    wlr_seat_set_primary_selection(s->seat, event->source, event->serial);
}
