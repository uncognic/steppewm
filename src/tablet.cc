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


#include "wlr.h"

#include <cstdlib>
#include <linux/input-event-codes.h>

#include "input.h"
#include "output.h"
#include "server.h"
#include "tablet.h"
#include "view.h"

#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/util/box.h>

using namespace steppewm;

void tablet_tool::map_to_layout(const server *s, const double tx, const double ty, double *lx, double *ly) {
    wlr_box layout_box;
    wlr_output_layout_get_box(s->output_layout, nullptr, &layout_box);
    if (layout_box.width <= 0 || layout_box.height <= 0) {
        *lx = 0;
        *ly = 0;
        return;
    }
    *lx = layout_box.x + tx * layout_box.width;
    *ly = layout_box.y + ty * layout_box.height;
}

wlr_surface *tablet_tool::surface_at(const server *s, const double lx, const double ly, double *sx, double *sy) {
    wlr_scene_node *node = wlr_scene_node_at(&s->scene->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
        return nullptr;
    }
    auto *scene_buffer = wlr_scene_buffer_from_node(node);
    const auto *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    return scene_surface ? scene_surface->surface : nullptr;
}

void tablet_tool::create(server *s, wlr_input_device *device) {
    auto *t = new tablet_tool();
    t->srv = s;
    t->tablet = wlr_tablet_from_input_device(device);
    t->wlr_tool = nullptr;
    t->tool_v2 = nullptr;

    t->tablet_v2 = wlr_tablet_create(s->tablet_mgr, s->seat, device);

    t->axis.connect(&t->tablet->events.axis,
                    [t](void *data) { t->handle_axis(data); });
    t->proximity.connect(&t->tablet->events.proximity,
                         [t](void *data) { t->handle_proximity(data); });
    t->tip.connect(&t->tablet->events.tip,
                   [t](void *data) { t->handle_tip(data); });
    t->button.connect(&t->tablet->events.button,
                      [t](void *data) { t->handle_button(data); });
    t->device_destroy.connect(&device->events.destroy,
                              [t](void *) { t->destroy(); });

    wl_list_insert(&s->tablet_tools, &t->link);
}

void tablet_tool::handle_axis(void *data) const {
    const auto *event = static_cast<wlr_tablet_tool_axis_event *>(data);

    double lx, ly;
    map_to_layout(srv, event->x, event->y, &lx, &ly);
    wlr_cursor_warp(srv->cursor, nullptr, lx, ly);
    server::process_cursor_motion(srv, event->time_msec);

    if (!tool_v2) {
        return;
    }

    const uint32_t updated = event->updated_axes;

    if (updated & WLR_TABLET_TOOL_AXIS_X || updated & WLR_TABLET_TOOL_AXIS_Y) {
        wlr_tablet_v2_tablet_tool_notify_motion(tool_v2, lx, ly);
    }
    if (updated & WLR_TABLET_TOOL_AXIS_PRESSURE) {
        wlr_tablet_v2_tablet_tool_notify_pressure(tool_v2, event->pressure);
    }
    if (updated & WLR_TABLET_TOOL_AXIS_DISTANCE) {
        wlr_tablet_v2_tablet_tool_notify_distance(tool_v2, event->distance);
    }
    if (updated & WLR_TABLET_TOOL_AXIS_TILT_X || updated & WLR_TABLET_TOOL_AXIS_TILT_Y) {
        wlr_tablet_v2_tablet_tool_notify_tilt(tool_v2, event->tilt_x, event->tilt_y);
    }
    if (updated & WLR_TABLET_TOOL_AXIS_ROTATION) {
        wlr_tablet_v2_tablet_tool_notify_rotation(tool_v2, event->rotation);
    }
    if (updated & WLR_TABLET_TOOL_AXIS_SLIDER) {
        wlr_tablet_v2_tablet_tool_notify_slider(tool_v2, event->slider);
    }
    if (updated & WLR_TABLET_TOOL_AXIS_WHEEL) {
        wlr_tablet_v2_tablet_tool_notify_wheel(tool_v2, event->wheel_delta, 0);
    }
}

void tablet_tool::handle_proximity(void *data) {
    const auto *event = static_cast<wlr_tablet_tool_proximity_event *>(data);
    wlr_tool = event->tool;

    double lx, ly;
    map_to_layout(srv, event->x, event->y, &lx, &ly);
    wlr_cursor_warp(srv->cursor, nullptr, lx, ly);
    server::process_cursor_motion(srv, event->time_msec);

    if (event->state == WLR_TABLET_TOOL_PROXIMITY_IN) {
        if (!event->tool->data) {
            tool_v2 = wlr_tablet_tool_create(srv->tablet_mgr, srv->seat, event->tool);
            event->tool->data = tool_v2;
        } else {
            tool_v2 = static_cast<wlr_tablet_v2_tablet_tool *>(event->tool->data);
        }

        double sx, sy;
        wlr_surface *surface = surface_at(srv, lx, ly, &sx, &sy);
        if (surface && wlr_surface_accepts_tablet_v2(surface, tablet_v2)) {
            wlr_tablet_v2_tablet_tool_notify_proximity_in(tool_v2, tablet_v2, surface);
        } else {
            wlr_tablet_v2_tablet_tool_notify_proximity_in(tool_v2, tablet_v2, nullptr);
        }
    } else {
        if (tool_v2) {
            wlr_tablet_v2_tablet_tool_notify_proximity_out(tool_v2);
        }
        tool_v2 = nullptr;
        wlr_tool = nullptr;
    }
}

void tablet_tool::handle_tip(void *data) const {
    const auto *event = static_cast<wlr_tablet_tool_tip_event *>(data);

    double lx, ly;
    map_to_layout(srv, event->x, event->y, &lx, &ly);
    wlr_cursor_warp(srv->cursor, nullptr, lx, ly);
    server::process_cursor_motion(srv, event->time_msec);

    if (!tool_v2) {
        return;
    }

    if (event->state == WLR_TABLET_TOOL_TIP_DOWN) {
        double sx, sy;
        wlr_surface *surface = nullptr;
        view *v = view::at(srv, lx, ly, &surface, &sx, &sy);
        if (v && surface) {
            v->focus(surface);
        }

        wlr_seat_pointer_notify_enter(srv->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(srv->seat, event->time_msec, sx, sy);
        wlr_seat_pointer_notify_button(srv->seat, event->time_msec, BTN_LEFT,
                                       WL_POINTER_BUTTON_STATE_PRESSED);

        wlr_tablet_v2_tablet_tool_notify_down(tool_v2);
    } else {
        wlr_seat_pointer_notify_button(srv->seat, event->time_msec, BTN_LEFT,
                                       WL_POINTER_BUTTON_STATE_RELEASED);

        wlr_tablet_v2_tablet_tool_notify_up(tool_v2);
    }
}

void tablet_tool::handle_button(void *data) const {
    const auto *event = static_cast<wlr_tablet_tool_button_event *>(data);
    if (!tool_v2) {
        return;
    }

    const enum zwp_tablet_pad_v2_button_state state =
            event->state == WLR_BUTTON_PRESSED
                ? ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED
                : ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED;
    wlr_tablet_v2_tablet_tool_notify_button(tool_v2, event->button, state);
}

void tablet_tool::destroy() {
    wl_list_remove(&link);
    delete this;
}

void tablet_pad::create(server *s, wlr_input_device *device) {
    auto *p = new tablet_pad();
    p->srv = s;
    p->pad = wlr_tablet_pad_from_input_device(device);

    p->pad_v2 = wlr_tablet_pad_create(s->tablet_mgr, s->seat, device);

    p->button.connect(&p->pad->events.button,
                      [p](void *data) { p->handle_button(data); });
    p->ring.connect(&p->pad->events.ring,
                    [p](void *data) { p->handle_ring(data); });
    p->strip.connect(&p->pad->events.strip,
                     [p](void *data) { p->handle_strip(data); });
    p->device_destroy.connect(&device->events.destroy,
                              [p](void *) { p->destroy(); });

    wl_list_insert(&s->tablet_pads, &p->link);
}

void tablet_pad::handle_button(void *data) const {
    const auto *event = static_cast<wlr_tablet_pad_button_event *>(data);
    const enum zwp_tablet_pad_v2_button_state state =
            event->state == WLR_BUTTON_PRESSED
                ? ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED
                : ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED;
    wlr_tablet_v2_tablet_pad_notify_button(pad_v2, event->button, event->time_msec, state);
}

void tablet_pad::handle_ring(void *data) const {
    const auto *event = static_cast<wlr_tablet_pad_ring_event *>(data);
    const bool finger = event->source == WLR_TABLET_PAD_RING_SOURCE_FINGER;
    wlr_tablet_v2_tablet_pad_notify_ring(pad_v2, event->ring, event->position, finger,
                                         event->time_msec);
}

void tablet_pad::handle_strip(void *data) const {
    const auto *event = static_cast<wlr_tablet_pad_strip_event *>(data);
    const bool finger = event->source == WLR_TABLET_PAD_STRIP_SOURCE_FINGER;
    wlr_tablet_v2_tablet_pad_notify_strip(pad_v2, event->strip, event->position, finger,
                                          event->time_msec);
}

void tablet_pad::destroy() {
    wl_list_remove(&link);
    delete this;
}
