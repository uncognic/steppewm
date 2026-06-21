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

#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#include <libinput.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "input.h"
#include "lock.h"
#include "osd.h"
#include "output.h"
#include "server.h"
#include "switcher.h"
#include "tablet.h"
#include "taskbar.h"
#include "tray.h"
#include "view.h"

using namespace steppewm;

// keyboard
void server::spawn(const char* cmd) {
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
        if (out->output_taskbar) {
            out->output_taskbar->refresh();
        }
    }
}

view* server::focused_view(server* s) {
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

void server::dispatch_action(server* s, const char* action, const char* arg, uint32_t mods) {
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
        config::reload(s);
    } else if (strcmp(action, "workspace") == 0) {
        if (arg && arg[0]) {
            view::workspace_switch(s, atoi(arg) - 1);
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
        } else if (strcmp(action, "fullscreen") == 0) {
            v->toggle_fullscreen();
        } else if (strcmp(action, "close") == 0) {
            wlr_xdg_toplevel_send_close(v->toplevel);
        } else if (strcmp(action, "snap_left") == 0) {
            v->snap_to(snap_edge::LEFT);
        } else if (strcmp(action, "snap_right") == 0) {
            v->snap_to(snap_edge::RIGHT);
        } else if (strcmp(action, "snap_top_left") == 0) {
            v->snap_to(snap_edge::TOP_LEFT);
        } else if (strcmp(action, "snap_top_right") == 0) {
            v->snap_to(snap_edge::TOP_RIGHT);
        } else if (strcmp(action, "snap_bottom_left") == 0) {
            v->snap_to(snap_edge::BOTTOM_LEFT);
        } else if (strcmp(action, "snap_bottom_right") == 0) {
            v->snap_to(snap_edge::BOTTOM_RIGHT);
        } else if (strcmp(action, "pin") == 0) {
            v->pinned = !v->pinned;
            if (!v->pinned) {
                v->workspace = s->current_workspace;
            }
            taskbar::refresh_taskbars(s);
        }
    }
}

bool server::handle_keybinding(server* s, uint32_t mods, xkb_keysym_t sym) {
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
void keyboard::handle_modifiers(keyboard* kbd) {
    // focus keyboard
    wlr_seat_set_keyboard(kbd->srv->seat, kbd->wlr_kb);

    // send modifier
    wlr_seat_keyboard_notify_modifiers(kbd->srv->seat, &kbd->wlr_kb->modifiers);

    wlr_idle_notifier_v1_notify_activity(kbd->srv->idle_notifier, kbd->srv->seat);

    switcher::handle_modifiers(kbd->srv, wlr_keyboard_get_modifiers(kbd->wlr_kb));

    uint32_t group = kbd->wlr_kb->modifiers.group;

    // if the group isn't the same as before, that means the key combo for switch was hit
    if (group != kbd->srv->layout_group) {
        kbd->srv->layout_group = group;
        refresh_taskbars(kbd->srv);

        if (kbd->srv->osd_overlay) {
            // parse layout name from something like us,ru,ua
            const char* tok = kbd->srv->cfg.xkb_layout;
            for (uint32_t i = 0; i < group && tok && *tok; i++) {
                const char* comma = strchr(tok, ',');
                tok = comma ? comma + 1 : nullptr;
            }
            char text[32];
            size_t n = 0;
            if (tok) {
                while (tok[n] && tok[n] != ',' && n + 1 < sizeof(text)) {
                    text[n] = static_cast<char>(toupper(static_cast<unsigned char>(tok[n])));
                    n++;
                }
            }
            if (n == 0) {
                snprintf(text, sizeof(text), "US");
            } else {
                text[n] = '\0';
            }
            kbd->srv->osd_overlay->show(text);
        }
    }
}

// handles key presses and releases
void keyboard::handle_key(keyboard* kbd, void* data) {
    // get the server from the keyboard
    server* s = kbd->srv;

    // get the key event
    struct wlr_keyboard_key_event* event = static_cast<struct wlr_keyboard_key_event*>(data);

    // get the seat from the server
    struct wlr_seat* seat = s->seat;

    wlr_idle_notifier_v1_notify_activity(s->idle_notifier, seat);

    if (s->locked) {
        session_lock::ensure_focus(s);
    }

    // convert libinput keycode to xkbcommmon keycode
    uint32_t keycode = event->keycode + 8;

    // make keycode into keysim
    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(kbd->wlr_kb->xkb_state, keycode, &syms);

    // check for keybinds, only when a modifier is held
    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(kbd->wlr_kb);
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
#ifdef HAVE_SDBUS
    if (!handled && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        auto* tray = static_cast<tray_host*>(s->tray);
        if (tray && tray->menu_open()) {
            for (int i = 0; i < nsyms; i++) {
                if (syms[i] == XKB_KEY_Escape) {
                    tray->close_menu();
                    handled = true;
                }
            }
        }
    }
#endif
    // check if keyboard shortcuts are inhibited for the focused surface
    bool shortcuts_inhibited = false;
    {
        wlr_keyboard_shortcuts_inhibitor_v1* inhibitor;
        wl_list_for_each(inhibitor, &s->shortcuts_inhibit_mgr->inhibitors, link) {
            if (inhibitor->active &&
                inhibitor->surface == s->seat->keyboard_state.focused_surface) {
                shortcuts_inhibited = true;
                break;
            }
        }
    }

    // no keybinds while the session is locked or shortcuts are inhibited
    if (!handled && !s->locked && !shortcuts_inhibited &&
        event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            handled = server::handle_keybinding(s, modifiers, syms[i]) || handled;
        }

        if (!handled) {
            struct xkb_keymap* keymap = xkb_state_get_keymap(kbd->wlr_kb->xkb_state);
            xkb_layout_index_t layout = xkb_state_key_get_layout(kbd->wlr_kb->xkb_state, keycode);
            const xkb_keysym_t* raw_syms;
            int raw_nsyms = xkb_keymap_key_get_syms_by_level(keymap, keycode, layout, 0, &raw_syms);
            for (int i = 0; i < raw_nsyms; i++) {
                handled = server::handle_keybinding(s, modifiers, raw_syms[i]) || handled;
            }
        }
    }

    // if the key event wasnt a keybind, pass it to the focused client
    if (!handled) {
        wlr_seat_set_keyboard(seat, kbd->wlr_kb);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
    }
}

// destroy keyboard
void keyboard::handle_destroy(keyboard* kbd) {
    wl_list_remove(&kbd->link);
    delete kbd;
}

void keyboard::apply_config(server* s, struct wlr_keyboard* wlr_keyboard) {
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
void pointer::apply_config(server* s, struct wlr_input_device* device) {
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
void keyboard::create(server* s, struct wlr_input_device* device) {
    // create keyboard
    struct wlr_keyboard* wlr_keyboard = wlr_keyboard_from_input_device(device);

    // create keyboard
    auto* kbd = new keyboard();
    kbd->srv = s;
    kbd->wlr_kb = wlr_keyboard;

    apply_config(s, wlr_keyboard);

    // set up listeners
    kbd->modifiers.connect(&wlr_keyboard->events.modifiers,
                           [kbd](void*) { handle_modifiers(kbd); });
    kbd->key.connect(&wlr_keyboard->events.key, [kbd](void* data) { handle_key(kbd, data); });
    kbd->destroy.connect(&device->events.destroy, [kbd](void*) { handle_destroy(kbd); });

    wlr_seat_set_keyboard(s->seat, wlr_keyboard);

    wl_list_insert(&s->keyboards, &kbd->link);

    // show the layout indicator once a keyboard is present
    refresh_taskbars(s);
}

// destroy pointer
void pointer::handle_destroy(pointer* ptr) {
    wl_list_remove(&ptr->link);
    delete ptr;
}

// add pointer
void pointer::create(server* s, struct wlr_input_device* device) {
    wlr_cursor_attach_input_device(s->cursor, device);
    apply_config(s, device);

    // track the device so settings can be re-applied on config reload
    auto* ptr = new pointer();
    ptr->srv = s;
    ptr->device = device;
    ptr->destroy.connect(&device->events.destroy, [ptr](void*) { handle_destroy(ptr); });
    wl_list_insert(&s->pointers, &ptr->link);
}

void switch_device::handle_toggle(switch_device* sw, void* data) {
    const auto* event = static_cast<struct wlr_switch_toggle_event*>(data);
    server* s = sw->srv;

    switch_type type;
    switch_state state;

    switch (event->switch_type) {
        case WLR_SWITCH_TYPE_LID:
            type = switch_type::lid;
            break;
        case WLR_SWITCH_TYPE_TABLET_MODE:
            type = switch_type::tablet_mode;
            break;
        default:
            return;
    }
    state = event->switch_state == WLR_SWITCH_STATE_ON ? switch_state::on : switch_state::off;

    for (int i = 0; i < s->cfg.nswitchbinds; i++) {
        switchbind* b = &s->cfg.switchbinds[i];
        if (b->type == type && b->state == state) {
            server::dispatch_action(s, b->action, b->arg, 0);
        }
    }
}

void switch_device::handle_destroy(switch_device* sw) {
    wl_list_remove(&sw->link);
    delete sw;
}

void switch_device::create(server* s, struct wlr_input_device* device) {
    struct wlr_switch* wlr_sw = wlr_switch_from_input_device(device);

    auto* sw = new switch_device();
    sw->srv = s;
    sw->wlr_sw = wlr_sw;

    sw->toggle.connect(&wlr_sw->events.toggle, [sw](void* data) { handle_toggle(sw, data); });
    sw->destroy.connect(&device->events.destroy, [sw](void*) { handle_destroy(sw); });

    wl_list_insert(&s->switches, &sw->link);
}

// create new input
void server::on_new_input(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, new_input);
    auto* device = static_cast<struct wlr_input_device*>(data);

    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            keyboard::create(s, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            pointer::create(s, device);
            break;
        case WLR_INPUT_DEVICE_SWITCH:
            switch_device::create(s, device);
            break;
        case WLR_INPUT_DEVICE_TABLET:
            tablet_tool::create(s, device);
            break;
        case WLR_INPUT_DEVICE_TABLET_PAD:
            tablet_pad::create(s, device);
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
void server::input_reconfigure(server* s) {
    keyboard* kbd;
    wl_list_for_each(kbd, &s->keyboards, link) {
        keyboard::apply_config(s, kbd->wlr_kb);
    }

    pointer* ptr;
    wl_list_for_each(ptr, &s->pointers, link) {
        pointer::apply_config(s, ptr->device);
    }
}

// pointer constraints

void pointer_constraint::update(server* s) {
    struct wlr_surface* surface = s->seat->pointer_state.focused_surface;
    struct wlr_pointer_constraint_v1* constraint =
        surface ? wlr_pointer_constraints_v1_constraint_for_surface(s->pointer_constraints, surface,
                                                                    s->seat)
                : nullptr;

    struct wlr_pointer_constraint_v1* prev = s->active_constraint;
    if (prev == constraint) {
        return;
    }

    s->active_constraint = constraint;
    if (prev) {
        wlr_pointer_constraint_v1_send_deactivated(prev);
    }
    if (constraint) {
        wlr_pointer_constraint_v1_send_activated(constraint);
    }
}

pointer_constraint::pointer_constraint(server* s, struct wlr_pointer_constraint_v1* constraint)
    : srv(s), constraint(constraint) {
    destroy.connect(&constraint->events.destroy, [this](void*) { handle_destroy(); });
}

void pointer_constraint::handle_destroy() const {
    server* s = srv;

    if (s->active_constraint == constraint) {
        if (constraint->current.cursor_hint.enabled &&
            constraint->surface == s->seat->pointer_state.focused_surface) {
            const double sx = constraint->current.cursor_hint.x;
            const double sy = constraint->current.cursor_hint.y;

            const double lx = s->cursor->x - s->seat->pointer_state.sx + sx;
            const double ly = s->cursor->y - s->seat->pointer_state.sy + sy;
            wlr_cursor_warp(s->cursor, nullptr, lx, ly);
            wlr_seat_pointer_warp(s->seat, sx, sy);
        }
        s->active_constraint = nullptr;
    }

    delete this;
}

void pointer_constraint::init(server* s) {
    s->relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(s->display);

    s->pointer_constraints = wlr_pointer_constraints_v1_create(s->display);
    s->new_constraint.notify = on_new;
    wl_signal_add(&s->pointer_constraints->events.new_constraint, &s->new_constraint);
}

// handle new pointer constraints from clients
void pointer_constraint::on_new(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, new_constraint);
    auto* constraint = static_cast<struct wlr_pointer_constraint_v1*>(data);

    new pointer_constraint(s, constraint);

    if (constraint->surface == s->seat->pointer_state.focused_surface) {
        update(s);
    }
}

// idle inhibit

// true if the inhibitor's surface should currently block idle
bool idle_inhibitor::visible(server* s, struct wlr_surface* surface) {
    if (!surface->mapped) {
        return false;
    }

    // a toplevel only inhibits while it is actually shown
    struct wlr_xdg_surface* xdg = wlr_xdg_surface_try_from_wlr_surface(surface);
    if (xdg && xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL && xdg->toplevel->base->data) {
        const auto v = static_cast<view*>(xdg->toplevel->base->data);
        return v->mapped && !v->minimized && v->workspace == s->current_workspace;
    }

    return true;
}

// check if we should be inhibiting idle right now
void idle_inhibitor::update(server* s, const wlr_idle_inhibitor_v1* exclude) {
    bool inhibited = false;
    wlr_idle_inhibitor_v1* inhibitor;
    wl_list_for_each(inhibitor, &s->idle_inhibit_mgr->inhibitors, link) {
        if (inhibitor != exclude && visible(s, inhibitor->surface)) {
            inhibited = true;
            break;
        }
    }
    wlr_idle_notifier_v1_set_inhibited(s->idle_notifier, inhibited || s->idle_inhibit_manual);
}

idle_inhibitor::idle_inhibitor(server* s, struct wlr_idle_inhibitor_v1* inhibitor)
    : srv(s), inhibitor(inhibitor) {
    // recompute without the dying inhibitor, then self-destruct
    destroy.connect(&inhibitor->events.destroy, [this](void*) {
        update(srv, this->inhibitor);
        delete this;
    });

    update(s);
}

void idle_inhibitor::init(server* s) {
    s->idle_notifier = wlr_idle_notifier_v1_create(s->display);

    s->idle_inhibit_mgr = wlr_idle_inhibit_v1_create(s->display);
    s->new_idle_inhibitor.notify = idle_inhibitor::on_new;
    wl_signal_add(&s->idle_inhibit_mgr->events.new_inhibitor, &s->new_idle_inhibitor);
}

// handle new idle inhibitors from clients
void idle_inhibitor::on_new(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, new_idle_inhibitor);
    auto* inhibitor = static_cast<struct wlr_idle_inhibitor_v1*>(data);

    // owns itself, freed when the inhibitor is destroyed
    new idle_inhibitor(s, inhibitor);
}

//// cursor
// initiate move or resize operation for window
void server::cursor_begin_interactive(view* v, cursor_mode mode, uint32_t edges) {
    // get objects
    server* s = v->srv;
    struct wlr_scene_node* node = &v->scene_tree->node;

    s->grabbed_view = v;
    s->grab_mode = mode;
    s->grab_restore_pending = false;

    // calculate grab offsets
    if (mode == cursor_mode::move) {
        if (v->maximized || v->fullscreen || v->snapped != snap_edge::NONE) {
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

static snap_detect detect_snap(server* s) {
    snap_detect r = {snap_edge::NONE, false, {}};

    wlr_output* wlr_out = wlr_output_layout_output_at(s->output_layout, s->cursor->x, s->cursor->y);
    if (!wlr_out) {
        return r;
    }

    wlr_box ob;
    wlr_output_layout_get_box(s->output_layout, wlr_out, &ob);
    const bool at_left = s->cursor->x <= ob.x + 1;
    const bool at_right = s->cursor->x >= ob.x + ob.width - 2;
    const bool at_top = s->cursor->y <= ob.y + 1;
    const bool at_bottom = s->cursor->y >= ob.y + ob.height - 2;
    if (!at_left && !at_right && !at_top && !at_bottom) {
        return r;
    }

    const int usable_h = ob.height - output::taskbar_height(s, wlr_out);
    const int half_w = ob.width / 2;
    const int half_h = usable_h / 2;

    if (at_left && at_top) {
        r.edge = snap_edge::TOP_LEFT;
        r.zone = {ob.x, ob.y, half_w, half_h};
    } else if (at_right && at_top) {
        r.edge = snap_edge::TOP_RIGHT;
        r.zone = {ob.x + half_w, ob.y, ob.width - half_w, half_h};
    } else if (at_left && at_bottom) {
        r.edge = snap_edge::BOTTOM_LEFT;
        r.zone = {ob.x, ob.y + half_h, half_w, usable_h - half_h};
    } else if (at_right && at_bottom) {
        r.edge = snap_edge::BOTTOM_RIGHT;
        r.zone = {ob.x + half_w, ob.y + half_h, ob.width - half_w, usable_h - half_h};
    } else if (at_left) {
        r.edge = snap_edge::LEFT;
        r.zone = {ob.x, ob.y, half_w, usable_h};
    } else if (at_right) {
        r.edge = snap_edge::RIGHT;
        r.zone = {ob.x + half_w, ob.y, ob.width - half_w, usable_h};
    } else if (at_top) {
        r.maximize = true;
        r.zone = {ob.x, ob.y, ob.width, usable_h};
    }
    return r;
}

static void show_snap_indicator(const server* s, const wlr_box* zone) {
    const int gap = 4;
    wlr_scene_node_set_position(&s->snap_indicator->node, zone->x + gap, zone->y + gap);
    wlr_scene_rect_set_size(s->snap_indicator, zone->width - 2 * gap, zone->height - 2 * gap);
    wlr_scene_node_set_enabled(&s->snap_indicator->node, true);
    wlr_scene_node_raise_to_top(&s->snap_indicator->node);
}

static void hide_snap_indicator(server* s) {
    wlr_scene_node_set_enabled(&s->snap_indicator->node, false);
}

// distance the pointer must travel before a maximized window starts dragging
#define DRAG_THRESHOLD 5.0

// set cursor position
void server::process_cursor_move(server* s) {
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

    snap_detect sd = detect_snap(s);
    if (sd.edge != snap_edge::NONE || sd.maximize) {
        show_snap_indicator(s, &sd.zone);
    } else {
        hide_snap_indicator(s);
    }
}

// resize window with cursor movement
void server::process_cursor_resize(server* s) {
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

static void update_hover(server* s, wlr_scene_node* deco_node, view* deco_view) {
    // if the target changed
    if (s->hovered_deco_node != deco_node || s->hovered_deco_view != deco_view) {
        // remove the old decoration hover
        if (s->hovered_deco_view && s->hovered_deco_node) {
            s->hovered_deco_view->deco_set_hover(s->hovered_deco_node, false);
        }

        // set the new hover
        if (deco_view && deco_node) {
            deco_view->deco_set_hover(deco_node, true);
        }
        s->hovered_deco_node = deco_node;
        s->hovered_deco_view = deco_view;
    }

    // update taskbar hover state
    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        if (out->output_taskbar) {
            out->output_taskbar->hover_update(s->cursor->x, s->cursor->y);
        }
    }
}

// called on cursor motion events
void server::process_cursor_motion(server* s, uint32_t time_msec) {
    wlr_scene_node_set_position(&s->drag_icon_tree->node, static_cast<int>(s->cursor->x),
                                static_cast<int>(s->cursor->y));

    if (s->grab_mode == cursor_mode::move) {
        process_cursor_move(s);
        return;
    }
    if (s->grab_mode == cursor_mode::resize) {
        process_cursor_resize(s);
        return;
    }

#ifdef HAVE_SDBUS
    {
        auto* tray = static_cast<tray_host*>(s->tray);
        if (tray && tray->menu_open()) {
            tray->menu_hover(s->cursor->x, s->cursor->y);
        }
    }
#endif

    if (s->locked) {
        double sx, sy;
        struct wlr_surface* surface = nullptr;
        view::at(s, s->cursor->x, s->cursor->y, &surface, &sx, &sy);
        if (surface) {
            wlr_seat_pointer_notify_enter(s->seat, surface, sx, sy);
            wlr_seat_pointer_notify_motion(s->seat, time_msec, sx, sy);
        } else {
            wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, "default");
            wlr_seat_pointer_clear_focus(s->seat);
        }
        return;
    }

    double sx, sy;
    struct wlr_surface* surface = nullptr;
    // view::at populates surfaces from hit test, so swaybg doesn't show up since it doesn't have an
    // input region, but slurp does
    view::at(s, s->cursor->x, s->cursor->y, &surface, &sx, &sy);

    struct wlr_scene_node* hover_node = nullptr;
    view* hover_view = nullptr;

    struct wlr_seat* seat = s->seat;
    if (surface) {
        // deliver pointer events to whatever surface is under the cursor
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time_msec, sx, sy);
    } else {
        // empty area or decoration, pick the cursor ourselves and drop focus
        const char* cursor_name = "default";
        struct wlr_scene_node* hnode = nullptr;
        view* dview = view::deco_at(s, s->cursor->x, s->cursor->y, &hnode);
        const char* deco_cursor = dview ? dview->deco_cursor_name(hnode) : nullptr;
        if (deco_cursor) {
            cursor_name = deco_cursor;
        }

        if (dview && dview->deco_is_button(hnode)) {
            hover_view = dview;
            hover_node = hnode;
            cursor_name = "pointer";
        }

        wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, cursor_name);
        wlr_seat_pointer_clear_focus(seat);
    }

    update_hover(s, hover_node, hover_view);

    pointer_constraint::update(s);
}

// move the cursor, honoring any active constraint
void server::cursor_move_relative(server* s, struct wlr_input_device* device, double dx, double dy,
                                  double unaccel_dx, double unaccel_dy, uint32_t time_msec) {
    wlr_idle_notifier_v1_notify_activity(s->idle_notifier, s->seat);

    wlr_relative_pointer_manager_v1_send_relative_motion(s->relative_pointer_mgr, s->seat,
                                                         (uint64_t) time_msec * 1000, dx, dy,
                                                         unaccel_dx, unaccel_dy);

    if (s->active_constraint && s->grab_mode == cursor_mode::passthrough &&
        s->active_constraint->surface == s->seat->pointer_state.focused_surface) {
        if (s->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
            return;
        }

        double sx = s->seat->pointer_state.sx;
        double sy = s->seat->pointer_state.sy;
        double cx, cy;
        if (wlr_region_confine(&s->active_constraint->region, sx, sy, sx + dx, sy + dy, &cx, &cy)) {
            dx = cx - sx;
            dy = cy - sy;
        }
    }

    wlr_cursor_move(s->cursor, device, dx, dy);
    process_cursor_motion(s, time_msec);
}

// handle cursor motion events
void server::on_cursor_motion(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, cursor_motion);
    struct wlr_pointer_motion_event* event = static_cast<struct wlr_pointer_motion_event*>(data);

    // move the cursor
    cursor_move_relative(s, &event->pointer->base, event->delta_x, event->delta_y,
                         event->unaccel_dx, event->unaccel_dy, event->time_msec);
}

// handle absolute cursor motion events
void server::on_cursor_motion_absolute(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, cursor_motion_absolute);
    const auto* event = static_cast<struct wlr_pointer_motion_absolute_event*>(data);

    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(s->cursor, &event->pointer->base, event->x, event->y, &lx,
                                         &ly);
    const double dx = lx - s->cursor->x;
    const double dy = ly - s->cursor->y;
    cursor_move_relative(s, &event->pointer->base, dx, dy, dx, dy, event->time_msec);
}

// handle cursor button events
void server::on_cursor_button(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, cursor_button);
    const auto event = static_cast<struct wlr_pointer_button_event*>(data);

    wlr_idle_notifier_v1_notify_activity(s->idle_notifier, s->seat);

    // notify the seat of the event
    wlr_seat_pointer_notify_button(s->seat, event->time_msec, event->button, event->state);

    // if the button was released, end any operation
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        view* v = s->grabbed_view;
        const cursor_mode mode = s->grab_mode;
        s->grab_mode = cursor_mode::passthrough;
        s->grabbed_view = nullptr;
        s->grab_restore_pending = false;
        hide_snap_indicator(s);

        if (mode == cursor_mode::move && v && v->mapped) {
            const snap_detect sd = detect_snap(s);
            if (sd.maximize) {
                v->toggle_maximize();
            } else if (sd.edge != snap_edge::NONE) {
                v->snap_to(sd.edge);
            }
        }
        return;
    }

#ifdef HAVE_SDBUS
    {
        auto* tray = static_cast<tray_host*>(s->tray);
        if (tray && tray->menu_open()) {
            int mi = tray->menu_item_at(s->cursor->x, s->cursor->y);
            if (mi >= 0 && event->button == BTN_LEFT) {
                tray->menu_click(mi);
            } else {
                tray->close_menu();
            }
            return;
        }
    }
#endif

    if (s->locked) {
        return;
    }

    double sx, sy;
    struct wlr_surface* surface = nullptr;
    view* v = view::at(s, s->cursor->x, s->cursor->y, &surface, &sx, &sy);

    // alt drag for compositor initiated move/resize
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(s->seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    if (mods & WLR_MODIFIER_ALT && v) {
        v->focus(v->toplevel->base->surface);
        // left click for move
        if (event->button == BTN_LEFT) {
            cursor_begin_interactive(v, cursor_mode::move, 0);
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
            cursor_begin_interactive(v, cursor_mode::resize, edges);
            return;
        }
    }

    if (v) {
        v->focus(v->toplevel->base->surface);
        return;
    }

    // no view was clicked, check if a titlebar or border was clicked for move/resize
    struct wlr_scene_node* hnode = nullptr;
    view* dview = view::deco_at(s, s->cursor->x, s->cursor->y, &hnode);
    if (dview) {
        dview->focus(dview->toplevel->base->surface);
        dview->deco_handle_button(s, hnode, event->button, event->time_msec);
        return;
    }

    // tray icon click
    {
        output* out;
        wl_list_for_each(out, &s->outputs, link) {
            if (!out->output_taskbar) {
                continue;
            }
            int tray_idx = out->output_taskbar->tray_at(s->cursor->x, s->cursor->y);
            if (tray_idx >= 0) {
#ifdef HAVE_SDBUS
                auto* tray = static_cast<tray_host*>(s->tray);
                if (tray) {
                    if (event->button == BTN_LEFT) {
                        tray->activate(tray_idx, static_cast<int>(s->cursor->x),
                                       static_cast<int>(s->cursor->y));
                    } else if (event->button == BTN_MIDDLE) {
                        tray->secondary_activate(tray_idx, static_cast<int>(s->cursor->x),
                                                 static_cast<int>(s->cursor->y));
                    } else if (event->button == BTN_RIGHT) {
                        tray->context_menu(tray_idx, static_cast<int>(s->cursor->x),
                                           static_cast<int>(s->cursor->y));
                    }
                }
#endif
                return;
            }
        }
    }

    // if a taskbar item was clicked
    if (event->button == BTN_LEFT) {
        output* out;
        wl_list_for_each(out, &s->outputs, link) {
            // prevent dereferencing NULL on outputs that don't have a taskbar
            if (!out->output_taskbar) {
                continue;
            }

            // check if any buttons were pressed
            if (out->output_taskbar->idle_inhibit_at(s->cursor->x, s->cursor->y)) {
                // toggle it
                s->idle_inhibit_manual = !s->idle_inhibit_manual;
                idle_inhibitor::update(s);
                taskbar::refresh_taskbars(s);
                return;
            }
            int ws = out->output_taskbar->workspace_at(s->cursor->x, s->cursor->y);
            if (ws >= 0) {
                view::workspace_switch(s, ws);
                return;
            }
            view* tv = out->output_taskbar->view_at(s->cursor->x, s->cursor->y);
            if (tv) {
                if (!tv->pinned && tv->workspace != s->current_workspace) {
                    view::workspace_switch(s, tv->workspace);
                }
                tv->focus(tv->toplevel->base->surface);
                return;
            }
            int pin_idx = out->output_taskbar->pin_at(s->cursor->x, s->cursor->y);
            if (pin_idx >= 0) {
                server::spawn(s->cfg.pins[pin_idx].command);
                return;
            }
        }
    }

    if (event->button == BTN_MIDDLE) {
        output* out;
        wl_list_for_each(out, &s->outputs, link) {
            // prevent dereferencing NULL on outputs that don't have a taskbar
            if (!out->output_taskbar) {
                continue;
            }

            if (out->output_taskbar->volume_at(s->cursor->x, s->cursor->y)) {
                spawn(s->cfg.volume_middle_button);
            }
        }
    }
}

// handle cursor scroll wheel / axis events
void server::on_cursor_axis(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, cursor_axis);
    struct wlr_pointer_axis_event* event = static_cast<struct wlr_pointer_axis_event*>(data);

    wlr_idle_notifier_v1_notify_activity(s->idle_notifier, s->seat);

    if (!s->locked && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        output* out;
        wl_list_for_each(out, &s->outputs, link) {
            if (!out->output_taskbar) {
                continue;
            }
            if (out->output_taskbar->brightness_at(s->cursor->x, s->cursor->y)) {
                const char* cmd =
                    event->delta < 0 ? s->cfg.brightness_scroll_up : s->cfg.brightness_scroll_down;
                if (cmd[0]) {
                    spawn(cmd);
                }
                return;
            }
            if (out->output_taskbar->volume_at(s->cursor->x, s->cursor->y)) {
                const char* cmd =
                    event->delta < 0 ? s->cfg.volume_scroll_up : s->cfg.volume_scroll_down;
                if (cmd[0]) {
                    spawn(cmd);
                }
                return;
            }
        }
    }

    // forward scroll event to the seat
    wlr_seat_pointer_notify_axis(s->seat, event->time_msec, event->orientation, event->delta,
                                 event->delta_discrete, event->source, event->relative_direction);
}

// handle cursor frame events
void server::on_cursor_frame(struct wl_listener* listener, void* data) {
    (void) data;
    // get objects
    server* s = wl_container_of(listener, s, cursor_frame);

    // notify seat
    wlr_seat_pointer_notify_frame(s->seat);
}

//// seat requests

// handle cursor image change requests from clients
void server::on_request_set_cursor(struct wl_listener* listener, void* data) {
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
void server::on_request_set_shape(struct wl_listener* listener, void* data) {
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

// handle drag requests from clients
void server::on_request_start_drag(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, request_start_drag);
    const auto* event = static_cast<struct wlr_seat_request_start_drag_event*>(data);

    if (!s->locked &&
        wlr_seat_validate_pointer_grab_serial(s->seat, event->origin, event->serial)) {
        wlr_seat_start_pointer_drag(s->seat, event->drag, event->serial);
        return;
    }

    wlr_data_source_destroy(event->drag->source);
}

// the drag ended
void server::on_drag_destroy(struct wl_listener* listener, void* data) {
    (void) data;
    server* s = wl_container_of(listener, s, drag_destroy);
    wl_list_remove(&s->drag_destroy.link);
    process_cursor_motion(s, 0);
}

void server::on_start_drag(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, start_drag);
    auto* drag = static_cast<struct wlr_drag*>(data);

    s->drag_destroy.notify = on_drag_destroy;
    wl_signal_add(&drag->events.destroy, &s->drag_destroy);

    if (drag->icon) {
        wlr_scene_drag_icon_create(s->drag_icon_tree, drag->icon);
        wlr_scene_node_raise_to_top(&s->drag_icon_tree->node);
        wlr_scene_node_set_position(&s->drag_icon_tree->node, static_cast<int>(s->cursor->x),
                                    static_cast<int>(s->cursor->y));
    }
}

// handle clipboard / selection change requests from clients
void server::on_request_set_selection(struct wl_listener* listener, void* data) {
    // get objects
    server* s = wl_container_of(listener, s, request_set_selection);
    auto* event = static_cast<struct wlr_seat_request_set_selection_event*>(data);

    // update seat selection (clipboard)
    wlr_seat_set_selection(s->seat, event->source, event->serial);
}

// handle primary selection requests from clients
void server::on_request_set_primary_selection(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, request_set_primary_selection);
    auto* event = static_cast<struct wlr_seat_request_set_primary_selection_event*>(data);
    wlr_seat_set_primary_selection(s->seat, event->source, event->serial);
}
