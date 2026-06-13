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

#ifdef HAVE_LIBPULSE

#include <cstring>
#include <sys/eventfd.h>
#include <unistd.h>

#include <pulse/pulseaudio.h>
#include <wayland-server-core.h>

#include "wlr.hxx" // must be first

#include "listener.hxx"
#include "server.hxx"
#include "taskbar.hxx"
#include "view.hxx"
#include "volume.hxx"

using namespace steppewm;

// pa_threaded_mainloop runs pulse in a background thread
volume_monitor* volume_monitor::create(server* s) {
    auto* vm = new volume_monitor();
    vm->srv_ = s;

    vm->wake_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (vm->wake_fd_ < 0) {
        delete vm;
        return nullptr;
    }

    struct wl_event_loop* loop = wl_display_get_event_loop(s->display);
    vm->wake_source_ = wl_event_loop_add_fd(loop, vm->wake_fd_, WL_EVENT_READABLE, on_wake, vm);

    vm->mainloop_ = pa_threaded_mainloop_new();
    if (!vm->mainloop_) {
        wl_event_source_remove(vm->wake_source_);
        close(vm->wake_fd_);
        delete vm;
        return nullptr;
    }

    pa_mainloop_api* api = pa_threaded_mainloop_get_api(vm->mainloop_);
    vm->context_ = pa_context_new(api, "steppewm");
    if (!vm->context_) {
        pa_threaded_mainloop_free(vm->mainloop_);
        wl_event_source_remove(vm->wake_source_);
        close(vm->wake_fd_);
        delete vm;
        return nullptr;
    }

    pa_context_set_state_callback(vm->context_, on_context_state, vm);
    pa_context_connect(vm->context_, nullptr, PA_CONTEXT_NOFAIL, nullptr);
    pa_threaded_mainloop_start(vm->mainloop_);

    return vm;
}

volume_monitor::~volume_monitor() {
    if (mainloop_) {
        pa_threaded_mainloop_stop(mainloop_);
    }
    if (context_) {
        pa_context_disconnect(context_);
        pa_context_unref(context_);
    }
    if (mainloop_) {
        pa_threaded_mainloop_free(mainloop_);
    }
    if (wake_source_) {
        wl_event_source_remove(wake_source_);
    }
    if (wake_fd_ >= 0) {
        close(wake_fd_);
    }
}

// called from the pulse thread to wake the wayland event loop
void volume_monitor::notify() const {
    uint64_t val = 1;
    write(wake_fd_, &val, sizeof(val));
}

// runs on the main thread when the eventfd becomes readable
int volume_monitor::on_wake(int fd, uint32_t, void* data) {
    uint64_t val;
    read(fd, &val, sizeof(val));
    auto* vm = static_cast<volume_monitor*>(data);
    taskbar::refresh_taskbars(vm->srv_);
    return 0;
}

// subscribe to sink and server events once the connection is up
void volume_monitor::on_context_state(pa_context* c, void* userdata) {
    auto* vm = static_cast<volume_monitor*>(userdata);
    pa_context_state_t state = pa_context_get_state(c);

    if (state == PA_CONTEXT_READY) {
        pa_context_set_subscribe_callback(c,
            [](pa_context* ctx, pa_subscription_event_type_t t, uint32_t idx, void* ud) {
                on_event(ctx, static_cast<int>(t), idx, ud);
            }, vm);
        pa_context_subscribe(c,
            static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER),
            on_subscribe, nullptr);
        vm->query_default_sink();
    }
}

void volume_monitor::on_subscribe(pa_context*, int, void*) {}

// sink changed (volume or mute) or default server sink switched
void volume_monitor::on_event(pa_context* c, int type, uint32_t idx, void* userdata) {
    auto* vm = static_cast<volume_monitor*>(userdata);
    auto facility = static_cast<unsigned int>(type) & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;

    if (facility == PA_SUBSCRIPTION_EVENT_SINK) {
        pa_context_get_sink_info_by_index(c, idx,
            [](pa_context*, const pa_sink_info* info, int eol, void* ud) {
                if (eol || !info) return;
                on_sink_info(nullptr, info, 0, ud);
            }, vm);
    } else if (facility == PA_SUBSCRIPTION_EVENT_SERVER) {
        vm->query_default_sink();
    }
}

// only update if this sink is the default one we're tracking
void volume_monitor::on_sink_info(pa_context*, const void* info, int eol, void* userdata) {
    if (eol || !info) return;
    auto* vm = static_cast<volume_monitor*>(userdata);
    auto* si = static_cast<const pa_sink_info*>(info);

    if (vm->sink_name_[0] && strcmp(si->name, vm->sink_name_) != 0) {
        return;
    }

    pa_volume_t avg = pa_cvolume_avg(&si->volume);
    int pct = static_cast<int>((avg * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM);
    vm->volume_.store(pct);
    vm->muted_.store(si->mute != 0);
    vm->notify();
}

// default sink may have changed
void volume_monitor::on_server_info(pa_context* c, const void* info, void* userdata) {
    auto* vm = static_cast<volume_monitor*>(userdata);
    auto* si = static_cast<const pa_server_info*>(info);

    strncpy(vm->sink_name_, si->default_sink_name ? si->default_sink_name : "",
            sizeof(vm->sink_name_) - 1);

    pa_context_get_sink_info_by_name(c, vm->sink_name_,
        [](pa_context*, const pa_sink_info* sink, int eol, void* ud) {
            if (eol || !sink) return;
            on_sink_info(nullptr, sink, 0, ud);
        }, vm);
}

void volume_monitor::query_default_sink() {
    pa_context_get_server_info(context_,
        [](pa_context* c, const pa_server_info* info, void* ud) {
            on_server_info(c, info, ud);
        }, this);
}

#endif
