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

#include "wlr.hxx"

#include <cstdio>
#include <cstdlib>

#include "input.hxx"
#include "lock.hxx"
#include "output.hxx"
#include "paint.hxx"
#include "server.hxx"
#include "switcher.hxx"
#include "view.hxx"

using namespace steppewm;

// backdrop behind the lock surface
static constexpr float lock_bg_color[4] = {0.05f, 0.05f, 0.05f, 1.0f};

void session_lock::broken_msg_hide(server* s) {
    if (s->lock_msg_tree) {
        wlr_scene_node_destroy(&s->lock_msg_tree->node);
        s->lock_msg_tree = nullptr;
    }
}

// draw the message centered on one output
void session_lock::broken_msg_draw(server* s, const struct wlr_box* box) {
    const char* line1 = "The screen locker crashed and the session is still locked.";
    char line2[256];
    const char* sock = getenv("WAYLAND_DISPLAY");
    snprintf(line2, sizeof(line2),
             "To unlock, switch to another TTY (Ctrl+Alt+Fn) and run: \"WAYLAND_DISPLAY=%s "
             "swaylock\". Replace swaylock with your preferred locker.",
             sock ? sock : "<socket>");

    constexpr double font_size = 18.0;
    const cairo_text_extents_t e1 = paint::text_extents(line1, font_size);
    const cairo_text_extents_t e2 = paint::text_extents(line2, font_size);

    constexpr int pad = 24;
    constexpr int line_h = static_cast<int>(font_size * 1.8);
    const int w = static_cast<int>(e1.width > e2.width ? e1.width : e2.width) + pad * 2;
    constexpr int h = line_h * 2 + pad * 2;

    paint::Canvas canvas(w, h);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 1.0);

    cairo_move_to(cr, (w - e1.width) / 2.0 - e1.x_bearing, pad + font_size);
    cairo_show_text(cr, line1);
    cairo_move_to(cr, (w - e2.width) / 2.0 - e2.x_bearing, pad + line_h + font_size);
    cairo_show_text(cr, line2);

    wlr_scene_buffer* buf = wlr_scene_buffer_create(s->lock_msg_tree, nullptr);
    canvas.commit(buf);
    wlr_scene_node_set_position(&buf->node, box->x + (box->width - w) / 2,
                                box->y + (box->height - h) / 2);
}

void session_lock::broken_msg_show(server* s) {
    broken_msg_hide(s);
    s->lock_msg_tree = wlr_scene_tree_create(s->lock_tree);

    // show it on all outputs
    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        wlr_box box;
        wlr_output_layout_get_box(s->output_layout, out->wlr_output, &box);
        if (box.width > 0) {
            broken_msg_draw(s, &box);
        }
    }
}

void session_lock::init(server* s) {
    s->lock_tree = wlr_scene_tree_create(&s->scene->tree);
    wlr_scene_node_set_enabled(&s->lock_tree->node, false);

    s->lock_bg = wlr_scene_rect_create(s->lock_tree, 0, 0, lock_bg_color);

    s->session_lock_mgr = wlr_session_lock_manager_v1_create(s->display);
    s->new_lock.notify = on_new;
    wl_signal_add(&s->session_lock_mgr->events.new_lock, &s->new_lock);
}

// resize the backdrop to cover the whole layout
void session_lock::update_geometry(server* s) {
    if (!s->locked) {
        return;
    }

    wlr_box box;
    wlr_output_layout_get_box(s->output_layout, nullptr, &box);
    wlr_scene_node_set_position(&s->lock_bg->node, box.x, box.y);
    wlr_scene_rect_set_size(s->lock_bg, box.width, box.height);

    wlr_scene_node_raise_to_top(&s->lock_tree->node);

    // reposition and reconfigure every lock surface
    if (s->cur_lock) {
        struct wlr_session_lock_surface_v1* ls;
        wl_list_for_each(ls, &s->cur_lock->surfaces, link) {
            static_cast<lock_surface*>(ls->data)->configure();
        }
    }

    if (s->lock_msg_tree) {
        broken_msg_show(s);
    }
}

void session_lock::ensure_focus(server* s) {
    if (!s->locked || !s->cur_lock || wl_list_empty(&s->cur_lock->surfaces)) {
        return;
    }

    // already on a lock surface
    struct wlr_surface* focused = s->seat->keyboard_state.focused_surface;
    if (focused && wlr_session_lock_surface_v1_try_from_wlr_surface(focused)) {
        return;
    }

    struct wlr_session_lock_surface_v1* ls = wl_container_of(s->cur_lock->surfaces.next, ls, link);
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(s->seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(s->seat, ls->surface, keyboard->keycodes,
                                       keyboard->num_keycodes, &keyboard->modifiers);
    }
}

// handle new lock requests from clients
void session_lock::on_new(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, new_lock);
    auto* lock = static_cast<struct wlr_session_lock_v1*>(data);

    // refuse a second locker while one is running
    if (s->cur_lock) {
        wlr_session_lock_v1_destroy(lock);
        return;
    }

    new session_lock(s, lock);
}

session_lock::session_lock(server* s, struct wlr_session_lock_v1* lock) : srv(s) {
    s->cur_lock = lock;
    s->locked = true;

    broken_msg_hide(s);

    // cancel anything interactive
    switcher::cancel(s);
    s->grab_mode = cursor_mode::passthrough;
    s->grabbed_view = nullptr;
    s->grab_restore_pending = false;

    // take focus away from regular clients
    wlr_seat_keyboard_notify_clear_focus(s->seat);
    wlr_seat_pointer_clear_focus(s->seat);
    pointer_constraint::update(s);

    // blank all outputs
    wlr_scene_node_set_enabled(&s->lock_tree->node, true);
    update_geometry(s);

    new_surface.connect(&lock->events.new_surface, [this](void* data) {
        new lock_surface(srv, static_cast<wlr_session_lock_surface_v1*>(data));
    });

    unlock.connect(&lock->events.unlock, [this](void*) {
        srv->locked = false;
        wlr_scene_node_set_enabled(&srv->lock_tree->node, false);
        view::focus_next(srv, nullptr);
    });

    destroy.connect(&lock->events.destroy, [this](void*) {
        srv->cur_lock = nullptr;

        // locker crashed, show message
        if (srv->locked) {
            broken_msg_show(srv);
        }
        delete this;
    });

    wlr_session_lock_v1_send_locked(lock);
}

lock_surface::lock_surface(server* s, struct wlr_session_lock_surface_v1* lock_surface)
    : srv(s), wlr_lock_surface(lock_surface) {
    lock_surface->data = this;
    tree = wlr_scene_subsurface_tree_create(s->lock_tree, lock_surface->surface);
    configure();

    // focus
    const wlr_keyboard* keyboard = wlr_seat_get_keyboard(s->seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(s->seat, lock_surface->surface, keyboard->keycodes,
                                       keyboard->num_keycodes, &keyboard->modifiers);
    }

    destroy.connect(&lock_surface->events.destroy, [this](void*) { handle_destroy(); });
}

void lock_surface::configure() const {
    wlr_box box;
    wlr_output_layout_get_box(srv->output_layout, wlr_lock_surface->output, &box);
    wlr_scene_node_set_position(&tree->node, box.x, box.y);
    wlr_session_lock_surface_v1_configure(wlr_lock_surface, box.width, box.height);
}

void lock_surface::handle_destroy() const {
    server* s = srv;

    if (s->seat->keyboard_state.focused_surface == wlr_lock_surface->surface) {
        wlr_session_lock_surface_v1* other = nullptr;
        if (s->cur_lock) {
            wlr_session_lock_surface_v1* ls;
            wl_list_for_each(ls, &s->cur_lock->surfaces, link) {
                if (ls != wlr_lock_surface) {
                    other = ls;
                    break;
                }
            }
        }

        wlr_keyboard* keyboard = wlr_seat_get_keyboard(s->seat);
        if (other && keyboard) {
            wlr_seat_keyboard_notify_enter(s->seat, other->surface, keyboard->keycodes,
                                           keyboard->num_keycodes, &keyboard->modifiers);
        } else if (s->locked) {
            wlr_seat_keyboard_notify_clear_focus(s->seat);
        }
    }

    delete this;
}
