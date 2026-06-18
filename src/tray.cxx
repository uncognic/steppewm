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

#ifdef HAVE_SDBUS

#include <arpa/inet.h>
#include <cstring>
#include <poll.h>
#include <unistd.h>
#include <unordered_map>

#include <cairo/cairo.h>
#include <sdbus-c++/sdbus-c++.h>
#include <wayland-server-core.h>

#ifdef HAVE_LIBRSVG
#include <librsvg/rsvg.h>
#endif

#include "wlr.hxx"

#include "config.hxx"
#include "listener.hxx"
#include "output.hxx"
#include "paint.hxx"
#include "server.hxx"
#include "taskbar.hxx"
#include "tray.hxx"
#include "view.hxx"

using namespace steppewm;

void tray_host::refresh_all() const {
    output* out;
    wl_list_for_each(out, &srv_->outputs, link) {
        if (out->output_taskbar) {
            out->output_taskbar->refresh();
        }
    }
}

// build the vtable entry by hand
static sdbus::MethodVTableItem raw_method(const char* name, const char* in_sig,
                                          sdbus::method_callback cb) {
    auto m = sdbus::registerMethod(name);
    m.inputSignature = sdbus::Signature{in_sig};
    m.outputSignature = sdbus::Signature{""};
    m.callbackHandler = std::move(cb);
    return m;
}

tray_host* tray_host::create(server* s) {
    auto* host = new tray_host();
    host->srv_ = s;

    try {
        host->conn_ = sdbus::createSessionBusConnection();
        host->conn_->requestName(sdbus::ServiceName{"org.kde.StatusNotifierWatcher"});

        host->watcher_obj_ =
            sdbus::createObject(*host->conn_, sdbus::ObjectPath{"/StatusNotifierWatcher"});

        host->watcher_obj_
            ->addVTable(
                raw_method("RegisterStatusNotifierItem", "s",
                           [host](sdbus::MethodCall call) {
                               std::string service;
                               call >> service;
                               // reply before doing anything
                               call.createReply().send();
                               host->handle_register(service, std::string{call.getSender()});
                           }),
                raw_method("RegisterStatusNotifierHost", "s",
                           [](const sdbus::MethodCall& call) { call.createReply().send(); }),
                sdbus::registerProperty("RegisteredStatusNotifierItems").withGetter([host] {
                    std::vector<std::string> names;
                    names.reserve(host->items_.size());
                    for (const auto& item : host->items_) {
                        names.push_back(item->bus_name);
                    }
                    return names;
                }),
                sdbus::registerProperty("IsStatusNotifierHostRegistered").withGetter([]() -> bool {
                    return true;
                }),
                sdbus::registerProperty("ProtocolVersion").withGetter([]() -> int32_t {
                    return 0;
                }),
                sdbus::registerSignal("StatusNotifierItemRegistered").withParameters<std::string>(),
                sdbus::registerSignal("StatusNotifierItemUnregistered")
                    .withParameters<std::string>(),
                sdbus::registerSignal("StatusNotifierHostRegistered").withParameters<>())
            .forInterface(sdbus::InterfaceName{"org.kde.StatusNotifierWatcher"});

        // watch for bus names disappearing so we can remove dead tray items
        host->dbus_proxy_ =
            sdbus::createProxy(*host->conn_, sdbus::ServiceName{"org.freedesktop.DBus"},
                               sdbus::ObjectPath{"/org/freedesktop/DBus"});
        host->dbus_proxy_->uponSignal("NameOwnerChanged")
            .onInterface(sdbus::InterfaceName{"org.freedesktop.DBus"})
            .call(
                [host](const std::string& name, const std::string&, const std::string& new_owner) {
                    if (new_owner.empty()) {
                        host->handle_unregister(name);
                    }
                });

        // bridge sdbus fds into the wayland event loop so both run on one thread
        const auto pd = host->conn_->getEventLoopPollData();
        wl_event_loop* loop = wl_display_get_event_loop(s->display);

        host->dbus_source_ =
            wl_event_loop_add_fd(loop, pd.fd, WL_EVENT_READABLE, on_dbus_event, host);
        host->dbus_event_source_ =
            wl_event_loop_add_fd(loop, pd.eventFd, WL_EVENT_READABLE, on_dbus_event, host);
        host->dbus_timer_ = wl_event_loop_add_timer(loop, on_dbus_timer, host);
        host->update_event_source();

        host->watcher_obj_->emitSignal("StatusNotifierHostRegistered")
            .onInterface(sdbus::InterfaceName{"org.kde.StatusNotifierWatcher"});

        wlr_log(WLR_INFO, "tray: StatusNotifierWatcher active");
        return host;
    } catch (const sdbus::Error& e) {
        wlr_log(WLR_INFO, "tray: D-Bus watcher unavailable: %s", e.what());
        delete host;
        return nullptr;
    }
}

tray_host::~tray_host() {
    if (dbus_timer_) {
        wl_event_source_remove(dbus_timer_);
    }
    if (dbus_event_source_) {
        wl_event_source_remove(dbus_event_source_);
    }
    if (dbus_source_) {
        wl_event_source_remove(dbus_source_);
    }
    close_menu();
    if (conn_) {
        while (conn_->processPendingEvent()) {
        }
    }
    items_.clear();
    watcher_obj_.reset();
    dbus_proxy_.reset();
    conn_.reset();
}

// wayland callback
// drain pending sdbus messages and resync poll state
int tray_host::on_dbus_event(int, uint32_t, void* data) {
    auto* host = static_cast<tray_host*>(data);
    while (host->conn_->processPendingEvent()) {
    }
    host->update_event_source();
    return 0;
}

// wayland callback
// poll dbus
int tray_host::on_dbus_timer(void* data) {
    auto* host = static_cast<tray_host*>(data);
    while (host->conn_->processPendingEvent()) {
    }
    host->update_event_source();
    return 0;
}

void tray_host::update_event_source() const {
    const auto pd = conn_->getEventLoopPollData();

    uint32_t wl_mask = 0;
    if (pd.events & POLLIN) {
        wl_mask |= WL_EVENT_READABLE;
    }
    if (pd.events & POLLOUT) {
        wl_mask |= WL_EVENT_WRITABLE;
    }
    if (wl_mask == 0) {
        wl_mask = WL_EVENT_READABLE;
    }
    wl_event_source_fd_update(dbus_source_, wl_mask);

    int poll_ms = pd.getPollTimeout();
    if (poll_ms >= 0) {
        if (poll_ms < 1) {
            poll_ms = 1;
        }
        wl_event_source_timer_update(dbus_timer_, poll_ms);
    }
}

void tray_host::handle_register(const std::string& service, const std::string& sender) {
    if (service.empty()) {
        return;
    }

    std::string bus_name;
    std::string obj_path;

    if (service[0] == '/') {
        bus_name = sender;
        obj_path = service;
    } else {
        bus_name = service;
        obj_path = "/StatusNotifierItem";
    }

    for (const auto& item : items_) {
        if (item->bus_name == bus_name && item->object_path == obj_path) {
            return;
        }
    }

    auto item = std::make_unique<tray_item>();
    item->bus_name = bus_name;
    item->object_path = obj_path;

    try {
        item->proxy =
            sdbus::createProxy(*conn_, sdbus::ServiceName{bus_name}, sdbus::ObjectPath{obj_path});

        item->proxy->uponSignal("NewIcon")
            .onInterface(sdbus::InterfaceName{"org.kde.StatusNotifierItem"})
            .call([this, raw = item.get()]() {
                fetch_icon(raw);
                refresh_all();
            });
        item->proxy->uponSignal("NewAttentionIcon")
            .onInterface(sdbus::InterfaceName{"org.kde.StatusNotifierItem"})
            .call([this, raw = item.get()]() {
                fetch_icon(raw);
                refresh_all();
            });

        fetch_icon(item.get());
        fetch_menu_path(item.get());
    } catch (const sdbus::Error& e) {
        wlr_log(WLR_ERROR, "tray: proxy for %s failed: %s", bus_name.c_str(), e.what());
        return;
    }

    wlr_log(WLR_INFO, "tray: registered %s %s", bus_name.c_str(), obj_path.c_str());
    items_.push_back(std::move(item));

    watcher_obj_->emitSignal("StatusNotifierItemRegistered")
        .onInterface(sdbus::InterfaceName{"org.kde.StatusNotifierWatcher"})
        .withArguments(bus_name);

    refresh_all();
}

void tray_host::handle_unregister(const std::string& bus_name) {
    if (menu_item_ && menu_item_->bus_name == bus_name) {
        close_menu();
    }
    bool changed = false;
    for (auto it = items_.begin(); it != items_.end();) {
        if ((*it)->bus_name == bus_name) {
            wlr_log(WLR_INFO, "tray: unregistered %s", bus_name.c_str());
            watcher_obj_->emitSignal("StatusNotifierItemUnregistered")
                .onInterface(sdbus::InterfaceName{"org.kde.StatusNotifierWatcher"})
                .withArguments(bus_name);
            it = items_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) {
        refresh_all();
    }
}

// search xdg icon theme directories for an icon file matching name and size
static std::string find_icon_path_uncached(const std::string& name, const int target_size) {
    static const char* themes[] = {"hicolor", "Adwaita", "breeze"};
    static const char* exts[] = {".png", ".svg"};

    std::vector<std::string> base_dirs;
    const char* xdg = getenv("XDG_DATA_DIRS");
    if (xdg && xdg[0]) {
        std::string dirs(xdg);
        size_t pos = 0;
        while (pos < dirs.size()) {
            size_t sep = dirs.find(':', pos);
            if (sep == std::string::npos) {
                sep = dirs.size();
            }
            if (sep > pos) {
                base_dirs.push_back(dirs.substr(pos, sep - pos) + "/icons");
            }
            pos = sep + 1;
        }
    } else {
        base_dirs.emplace_back("/usr/share/icons");
        base_dirs.emplace_back("/usr/local/share/icons");
    }

    char sz_buf[16];
    snprintf(sz_buf, sizeof(sz_buf), "%d", target_size);
    std::string sz_str(sz_buf);

    static const char* categories[] = {"status",     "apps",    "devices", "actions",
                                       "categories", "emblems", "places"};

    for (auto& theme : themes) {
        for (auto& base : base_dirs) {
            for (auto& cat : categories) {
                for (auto& ext : exts) {
                    std::string path = base + "/" + theme + "/" + sz_str + "x" + sz_str + "/" +
                                       cat + "/" + name + ext;
                    if (access(path.c_str(), R_OK) == 0) {
                        return path;
                    }
                }
            }
            // scalable
            for (auto& cat : categories) {
                std::string path = base + "/" + theme + "/scalable/" + cat + "/" + name + ".svg";
                if (access(path.c_str(), R_OK) == 0) {
                    return path;
                }
            }
        }
    }

    // try common sizes as fallback
    static const int sizes[] = {48, 32, 24, 22, 16, 64, 128, 256};
    for (auto& theme : themes) {
        for (auto& base : base_dirs) {
            for (const auto s : sizes) {
                if (s == target_size) {
                    continue;
                }
                char sb[16];
                snprintf(sb, sizeof(sb), "%d", s);
                for (auto& cat : categories) {
                    for (auto& ext : exts) {
                        std::string path =
                            base + "/" + theme + "/" + sb + "x" + sb + "/" + cat + "/" + name + ext;
                        if (access(path.c_str(), R_OK) == 0) {
                            return path;
                        }
                    }
                }
            }
        }
    }

    // pixmaps fallback
    for (auto& ext : exts) {
        std::string path = "/usr/share/pixmaps/" + name + ext;
        if (access(path.c_str(), R_OK) == 0) {
            return path;
        }
    }

    return {};
}

static std::string find_icon_path(const std::string& name, const int target_size) {
    if (name.empty()) {
        return {};
    }
    if (name[0] == '/') {
        return access(name.c_str(), R_OK) == 0 ? name : std::string{};
    }

    static std::unordered_map<std::string, std::string> cache;
    std::string key = name + "#" + std::to_string(target_size);
    auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second;
    }

    std::string result = find_icon_path_uncached(name, target_size);
    cache.emplace(key, result);
    return result;
}

static bool load_icon_file(tray_item* item, const std::string& path,
                           [[maybe_unused]] const int target) {
    const bool is_svg = path.size() >= 4 && path.substr(path.size() - 4) == ".svg";

    cairo_surface_t* surface = nullptr;

    if (is_svg) {
#ifdef HAVE_LIBRSVG
        GError* err = nullptr;
        RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &err);
        if (!handle) {
            if (err) {
                g_error_free(err);
            }
            return false;
        }

        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target, target);
        cairo_t* cr = cairo_create(surface);

        RsvgRectangle viewport = {0, 0, (double) target, (double) target};
        rsvg_handle_render_document(handle, cr, &viewport, nullptr);

        cairo_destroy(cr);
        g_object_unref(handle);
#else
        return false;
#endif
    } else {
        surface = cairo_image_surface_create_from_png(path.c_str());
        if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            if (surface) {
                cairo_surface_destroy(surface);
            }
            return false;
        }
    }

    cairo_surface_flush(surface);
    int w = cairo_image_surface_get_width(surface);
    int h = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    unsigned char* data = cairo_image_surface_get_data(surface);

    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || !data) {
        cairo_surface_destroy(surface);
        return false;
    }

    item->icon_width = w;
    item->icon_height = h;
    item->icon_pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));

    for (int y = 0; y < h; y++) {
        auto* row = reinterpret_cast<uint32_t*>(data + static_cast<size_t>(y) * stride);
        memcpy(&item->icon_pixels[static_cast<size_t>(y) * w], row, w * sizeof(uint32_t));
    }
    item->icon_gen++;

    cairo_surface_destroy(surface);
    return true;
}

// width, height, ARGB32 pixels
using Pixmap = sdbus::Struct<int32_t, int32_t, std::vector<uint8_t>>;

// pick the closest-sized pixmap and convert to cairo's ARGB32
static bool decode_pixmaps(tray_item* item, const std::vector<Pixmap>& pixmaps, const int target) {
    if (pixmaps.empty()) {
        return false;
    }

    const Pixmap* best = &pixmaps[0];
    int best_diff = abs(std::get<0>(*best) - target);
    for (auto& pm : pixmaps) {
        int diff = abs(std::get<0>(pm) - target);
        if (diff < best_diff) {
            best = &pm;
            best_diff = diff;
        }
    }

    int w = std::get<0>(*best);
    int h = std::get<1>(*best);
    auto& data = std::get<2>(*best);

    // guard
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) {
        return false;
    }
    size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (data.size() < count * 4) {
        return false;
    }

    item->icon_width = w;
    item->icon_height = h;
    item->icon_pixels.resize(count);

    // convert from network-order straight alpha to host-order premultiplied
    auto* src = reinterpret_cast<const uint32_t*>(data.data());
    for (size_t i = 0; i < count; i++) {
        uint32_t px = ntohl(src[i]);
        uint8_t a = (px >> 24) & 0xff;
        uint8_t r = (px >> 16) & 0xff;
        uint8_t g = (px >> 8) & 0xff;
        uint8_t b = px & 0xff;
        if (a > 0 && a < 255) {
            r = static_cast<uint8_t>((r * a + 127) / 255);
            g = static_cast<uint8_t>((g * a + 127) / 255);
            b = static_cast<uint8_t>((b * a + 127) / 255);
        }
        item->icon_pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    item->icon_gen++;
    return true;
}

// async to not lock up the wm loop
void tray_host::fetch_icon(tray_item* item) {
    int target = srv_->cfg.taskbar_h;
    sdbus::InterfaceName iface{"org.kde.StatusNotifierItem"};

    item->proxy->getPropertyAsync("IconPixmap")
        .onInterface(iface)
        .uponReplyInvoke(
            [this, item, target](std::optional<sdbus::Error> err, sdbus::Variant value) {
                std::vector<Pixmap> pixmaps;
                if (!err) {
                    try {
                        pixmaps = value.get<std::vector<Pixmap>>();
                    } catch (const sdbus::Error&) {
                    }
                }
                if (decode_pixmaps(item, pixmaps, target)) {
                    refresh_all();
                    return;
                }
                fetch_icon_name(item);
            });
}

void tray_host::fetch_icon_name(tray_item* item) const {
    int target = srv_->cfg.taskbar_h;

    item->proxy->getPropertyAsync("IconName")
        .onInterface(sdbus::InterfaceName{"org.kde.StatusNotifierItem"})
        .uponReplyInvoke([this, item, target](std::optional<sdbus::Error> err,
                                              const sdbus::Variant& value) {
            if (err) {
                return;
            }
            std::string icon_name;
            try {
                icon_name = value.get<std::string>();
            } catch (const sdbus::Error&) {
                return;
            }
            if (icon_name.empty()) {
                return;
            }

            std::string path = find_icon_path(icon_name, target);
            if (!path.empty() && load_icon_file(item, path, target)) {
                wlr_log(WLR_DEBUG, "tray: loaded icon %s from %s", icon_name.c_str(), path.c_str());
                refresh_all();
                return;
            }
            wlr_log(WLR_DEBUG, "tray: icon %s not found on disk", icon_name.c_str());
        });
}

void tray_host::activate(const int index, const int x, const int y) const {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return;
    }
    try {
        items_[index]
            ->proxy->callMethod("Activate")
            .onInterface(sdbus::InterfaceName{"org.kde.StatusNotifierItem"})
            .withArguments(static_cast<int32_t>(x), static_cast<int32_t>(y))
            .dontExpectReply();
    } catch (const sdbus::Error&) {
    }
}

void tray_host::secondary_activate(const int index, const int x, const int y) const {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return;
    }
    try {
        items_[index]
            ->proxy->callMethod("SecondaryActivate")
            .onInterface(sdbus::InterfaceName{"org.kde.StatusNotifierItem"})
            .withArguments(static_cast<int32_t>(x), static_cast<int32_t>(y))
            .dontExpectReply();
    } catch (const sdbus::Error&) {
    }
}

// sni items have a "Menu" porperty with a dbus obj to a com.canonical.dbusmenu
void tray_host::fetch_menu_path(tray_item* item) {
    item->proxy->getPropertyAsync("Menu")
        .onInterface(sdbus::InterfaceName{"org.kde.StatusNotifierItem"})
        .uponReplyInvoke([item](std::optional<sdbus::Error> err, const sdbus::Variant& value) {
            if (err) {
                return;
            }
            try {
                item->menu_path = value.get<sdbus::ObjectPath>();
            } catch (const sdbus::Error&) {
            }
        });
}

// com.canonical.dbusmenu GetLayout returns a recursive struct
using DbusmenuLayout =
    sdbus::Struct<int32_t, std::map<std::string, sdbus::Variant>, std::vector<sdbus::Variant>>;

// dbusmenu labels use _, _Quit -> Quit
static std::string strip_mnemonics(const std::string& label) {
    std::string out;
    out.reserve(label.size());
    for (size_t i = 0; i < label.size(); i++) {
        if (label[i] == '_' && i + 1 < label.size()) {
            i++;
            out += label[i];
        } else {
            out += label[i];
        }
    }
    return out;
}

// flatten the root's immediate children into menu_entry structs
static void parse_layout(const DbusmenuLayout& layout, std::vector<menu_entry>& out) {
    auto& children = std::get<2>(layout);
    for (auto& child_var : children) {
        DbusmenuLayout child;
        try {
            child = child_var.get<DbusmenuLayout>();
        } catch (const sdbus::Error&) {
            continue;
        }
        auto& props = std::get<1>(child);
        menu_entry entry;
        entry.id = std::get<0>(child);

        auto get_str = [&](const char* key) -> std::string {
            auto it = props.find(key);
            if (it == props.end()) {
                return {};
            }
            try {
                return it->second.get<std::string>();
            } catch (...) {
                return {};
            }
        };
        auto get_bool = [&](const char* key, bool def) -> bool {
            auto it = props.find(key);
            if (it == props.end()) {
                return def;
            }
            try {
                return it->second.get<bool>();
            } catch (...) {
                return def;
            }
        };
        auto get_int = [&](const char* key, int32_t def) -> int32_t {
            auto it = props.find(key);
            if (it == props.end()) {
                return def;
            }
            try {
                return it->second.get<int32_t>();
            } catch (...) {
                return def;
            }
        };

        std::string type = get_str("type");
        entry.separator = (type == "separator");
        entry.label = strip_mnemonics(get_str("label"));
        entry.enabled = get_bool("enabled", true);
        entry.visible = get_bool("visible", true);
        entry.toggle_type = get_str("toggle-type");
        entry.toggle_state = get_int("toggle-state", -1);

        if (entry.visible) {
            out.push_back(std::move(entry));
        }
    }
}

// implement com.canonical.dbusmenu:
// fetch the menu layout over dbus, render it ourselves, and send click events back
void tray_host::context_menu(const int index, const int x, const int y) {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return;
    }

    auto* item = items_[index].get();

    if (menu_open() && menu_item_ == item) {
        close_menu();
        return;
    }
    close_menu();

    // fallback for apps that don't expose a dbusmenu object
    if (item->menu_path.empty()) {
        try {
            item->proxy->callMethod("ContextMenu")
                .onInterface(sdbus::InterfaceName{"org.kde.StatusNotifierItem"})
                .withArguments(static_cast<int32_t>(x), static_cast<int32_t>(y))
                .dontExpectReply();
        } catch (const sdbus::Error&) {
        }
        return;
    }

    try {
        menu_proxy_ = sdbus::createProxy(*conn_, sdbus::ServiceName{item->bus_name},
                                         sdbus::ObjectPath{item->menu_path});

        // AboutToShow tells the app to populate the menu
        bool needs_update = false;
        menu_proxy_->callMethod("AboutToShow")
            .onInterface(sdbus::InterfaceName{"com.canonical.dbusmenu"})
            .withArguments(int32_t(0))
            .storeResultsTo(needs_update);
    } catch (const sdbus::Error&) {
    }

    // fetch the menu tree
    // depth 1 = root's immediate children only
    try {
        uint32_t revision = 0;
        DbusmenuLayout layout;
        menu_proxy_->callMethod("GetLayout")
            .onInterface(sdbus::InterfaceName{"com.canonical.dbusmenu"})
            .withArguments(int32_t(0), int32_t(1), std::vector<std::string>{})
            .storeResultsTo(revision, layout);

        menu_entries_.clear();
        parse_layout(layout, menu_entries_);
    } catch (const sdbus::Error& e) {
        wlr_log(WLR_ERROR, "tray: GetLayout failed for %s: %s", item->bus_name.c_str(), e.what());
        menu_proxy_.reset();
        return;
    }

    if (menu_entries_.empty()) {
        menu_proxy_.reset();
        return;
    }

    menu_item_ = item;
    menu_hovered_ = -1;

    menu_tree_ = wlr_scene_tree_create(&srv_->scene->tree);
    menu_buf_ = wlr_scene_buffer_create(menu_tree_, nullptr);

    render_menu();

    menu_x_ = x - menu_w_ / 2;
    menu_y_ = y - menu_h_;

    wlr_scene_node_set_position(&menu_tree_->node, menu_x_, menu_y_);
    wlr_scene_node_raise_to_top(&menu_tree_->node);
}

void tray_host::render_menu() {
    const config* cfg = &srv_->cfg;
    const double font_size = cfg->taskbar_h * 0.55;
    const int pad_x = 12;
    const int pad_y = 4;
    const int sep_h = 7;

    int max_text_w = 0;
    for (auto& e : menu_entries_) {
        if (e.separator) {
            continue;
        }
        std::string display = e.label;
        if (!e.toggle_type.empty() && e.toggle_state == 1) {
            display = "\xe2\x9c\x93 " + display;
        }
        auto ext = paint::text_extents(display.c_str(), font_size);
        int w = static_cast<int>(ext.x_advance + 0.5);
        if (w > max_text_w) {
            max_text_w = w;
        }
    }

    menu_w_ = max_text_w + 2 * pad_x;
    if (menu_w_ < 100) {
        menu_w_ = 100;
    }

    menu_item_h_ = static_cast<int>(font_size + 2 * pad_y);
    menu_h_ = 0;
    for (auto& e : menu_entries_) {
        menu_h_ += e.separator ? sep_h : menu_item_h_;
    }

    paint::Canvas canvas(menu_w_, menu_h_);
    if (!canvas.valid()) {
        return;
    }
    cairo_t* cr = canvas.cr();

    const float* bg = cfg->color_taskbar_bg;
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], 1.0);
    cairo_paint(cr);

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);

    int cur_y = 0;
    for (int i = 0; i < static_cast<int>(menu_entries_.size()); i++) {
        auto& e = menu_entries_[i];

        if (e.separator) {
            const float* fg = cfg->color_task_text;
            cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], 0.3);
            cairo_rectangle(cr, pad_x, cur_y + sep_h / 2.0, menu_w_ - 2 * pad_x, 1);
            cairo_fill(cr);
            cur_y += sep_h;
            continue;
        }

        if (i == menu_hovered_ && e.enabled) {
            const float* ac = cfg->color_task_active;
            cairo_set_source_rgba(cr, ac[0], ac[1], ac[2], ac[3]);
            cairo_rectangle(cr, 0, cur_y, menu_w_, menu_item_h_);
            cairo_fill(cr);
        }

        const float* fg = cfg->color_task_text;
        double alpha = e.enabled ? 1.0 : 0.4;
        cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], alpha);

        std::string display = e.label;
        if (!e.toggle_type.empty() && e.toggle_state == 1) {
            display = "\xe2\x9c\x93 " + display;
        }

        cairo_move_to(cr, pad_x, cur_y + menu_item_h_ - pad_y - 2);
        cairo_show_text(cr, display.c_str());

        cur_y += menu_item_h_;
    }

    canvas.commit(menu_buf_);
}

void tray_host::close_menu() {
    if (menu_tree_) {
        wlr_scene_node_destroy(&menu_tree_->node);
        menu_tree_ = nullptr;
        menu_buf_ = nullptr;
    }
    menu_item_ = nullptr;
    menu_proxy_.reset();
    menu_entries_.clear();
    menu_hovered_ = -1;
}

int tray_host::menu_item_at(const double x, const double y) const {
    if (!menu_tree_) {
        return -1;
    }
    double lx = x - menu_x_;
    double ly = y - menu_y_;
    if (lx < 0 || lx >= menu_w_ || ly < 0 || ly >= menu_h_) {
        return -1;
    }

    const int sep_h = 7;
    int cur_y = 0;
    for (int i = 0; i < static_cast<int>(menu_entries_.size()); i++) {
        int item_h = menu_entries_[i].separator ? sep_h : menu_item_h_;
        if (ly >= cur_y && ly < cur_y + item_h) {
            if (menu_entries_[i].separator || !menu_entries_[i].enabled) {
                return -1;
            }
            return i;
        }
        cur_y += item_h;
    }
    return -1;
}

void tray_host::menu_click(const int item_index) {
    if (item_index < 0 || item_index >= static_cast<int>(menu_entries_.size())) {
        close_menu();
        return;
    }
    auto& entry = menu_entries_[item_index];
    if (!entry.enabled || entry.separator) {
        return;
    }

    // send the "clicked" event back to the app via dbusmenu
    try {
        menu_proxy_->callMethod("Event")
            .onInterface(sdbus::InterfaceName{"com.canonical.dbusmenu"})
            .withArguments(entry.id, std::string{"clicked"},
                           sdbus::Variant{static_cast<int32_t>(0)}, static_cast<uint32_t>(0))
            .dontExpectReply();
    } catch (const sdbus::Error& e) {
        wlr_log(WLR_ERROR, "tray: menu Event failed: %s", e.what());
    }

    close_menu();
}

void tray_host::menu_hover(const double x, const double y) {
    if (!menu_tree_) {
        return;
    }
    if (int idx = menu_item_at(x, y); idx != menu_hovered_) {
        menu_hovered_ = idx;
        render_menu();
        wlr_scene_node_raise_to_top(&menu_tree_->node);
    }
}

#endif
