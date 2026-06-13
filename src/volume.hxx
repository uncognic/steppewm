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

#pragma once

#ifdef HAVE_LIBPULSE

#include <atomic>
#include <cstdint>

struct pa_threaded_mainloop;
struct pa_context;
struct wl_event_source;

namespace steppewm {

class server;

class volume_monitor {
  public:
    static volume_monitor* create(server* s);
    ~volume_monitor();

    [[nodiscard]] int volume() const { return volume_.load(); }
    [[nodiscard]] bool muted() const { return muted_.load(); }

  private:
    volume_monitor() = default;

    static void on_context_state(pa_context* c, void* userdata);
    static void on_subscribe(pa_context* c, int success, void* userdata);
    static void on_event(pa_context* c, int type, uint32_t idx, void* userdata);
    static void on_sink_info(pa_context* c, const void* info, int eol, void* userdata);
    static void on_server_info(pa_context* c, const void* info, void* userdata);
    static int on_wake(int fd, uint32_t mask, void* data);
    void query_default_sink();
    void notify() const;

    pa_threaded_mainloop* mainloop_ = nullptr;
    pa_context* context_ = nullptr;
    server* srv_ = nullptr;
    int wake_fd_ = -1;
    wl_event_source* wake_source_ = nullptr;
    std::atomic<int> volume_{-1};
    std::atomic<bool> muted_{false};
    char sink_name_[256]{};
};

} // namespace steppewm

#endif
