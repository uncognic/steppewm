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

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include "config.hxx"
#include "input.hxx"
#include "output.hxx"
#include "server.hxx"
#include "taskbar.hxx"
#include "view.hxx"

using namespace steppewm;

uint32_t config::parse_modifiers(const char* str) {
    uint32_t mods = 0;
    char buf[64];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* saveptr;
    char* tok = strtok_r(buf, "+", &saveptr);
    while (tok) {
        if (strcasecmp(tok, "alt") == 0) {
            mods |= WLR_MODIFIER_ALT;
        } else if (strcasecmp(tok, "super") == 0 || strcasecmp(tok, "mod4") == 0) {
            mods |= WLR_MODIFIER_LOGO;
        } else if (strcasecmp(tok, "shift") == 0) {
            mods |= WLR_MODIFIER_SHIFT;
        } else if (strcasecmp(tok, "ctrl") == 0 || strcasecmp(tok, "control") == 0) {
            mods |= WLR_MODIFIER_CTRL;
        }
        tok = strtok_r(nullptr, "+", &saveptr);
    }
    return mods;
}

// queue command to be run
int config::lua_exec(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "steppewm_cfg");
    auto* cfg = static_cast<config*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    if (cfg->nexecs >= CFG_MAX_EXECS) {
        return luaL_error(L, "too many exec() calls (max %d)", CFG_MAX_EXECS);
    }

    const char* cmd = luaL_checkstring(L, 1);
    strncpy(cfg->execs[cfg->nexecs++], cmd, CFG_MAX_CMD - 1);
    return 0;
}

// bind()
int config::lua_bind(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "steppewm_cfg");
    auto* cfg = static_cast<config*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    if (cfg->nbinds >= CFG_MAX_BINDS) {
        return luaL_error(L, "too many keybindings (max %d)", CFG_MAX_BINDS);
    }

    const char* mods_str = luaL_checkstring(L, 1);
    const char* key_str = luaL_checkstring(L, 2);
    const char* action = luaL_checkstring(L, 3);

    xkb_keysym_t sym = xkb_keysym_from_name(key_str, XKB_KEYSYM_CASE_INSENSITIVE);
    if (sym == XKB_KEY_NoSymbol) {
        return luaL_error(L, "unknown key name '%s'", key_str);
    }

    keybind* b = &cfg->binds[cfg->nbinds++];
    b->modifiers = parse_modifiers(mods_str);
    b->sym = sym;
    strncpy(b->action, action, sizeof(b->action) - 1);

    if (lua_gettop(L) >= 4 && lua_isstring(L, 4)) {
        strncpy(b->arg, lua_tostring(L, 4), sizeof(b->arg) - 1);
    }

    return 0;
}

int config::parse_transform(const char* str) {
    if (strcasecmp(str, "normal") == 0 || strcmp(str, "0") == 0) {
        return WL_OUTPUT_TRANSFORM_NORMAL;
    }
    if (strcmp(str, "90") == 0) {
        return WL_OUTPUT_TRANSFORM_90;
    }
    if (strcmp(str, "180") == 0) {
        return WL_OUTPUT_TRANSFORM_180;
    }
    if (strcmp(str, "270") == 0) {
        return WL_OUTPUT_TRANSFORM_270;
    }
    if (strcasecmp(str, "flipped") == 0) {
        return WL_OUTPUT_TRANSFORM_FLIPPED;
    }
    if (strcasecmp(str, "flipped-90") == 0) {
        return WL_OUTPUT_TRANSFORM_FLIPPED_90;
    }
    if (strcasecmp(str, "flipped-180") == 0) {
        return WL_OUTPUT_TRANSFORM_FLIPPED_180;
    }
    if (strcasecmp(str, "flipped-270") == 0) {
        return WL_OUTPUT_TRANSFORM_FLIPPED_270;
    }
    return -1;
}

int config::lua_output(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "steppewm_cfg");
    auto* cfg = static_cast<config*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    // reuse the slot if this output was already configured
    output_config* oc = nullptr;
    for (int i = 0; i < cfg->noutput_cfgs; i++) {
        if (strcmp(cfg->output_cfgs[i].name, name) == 0) {
            oc = &cfg->output_cfgs[i];
            break;
        }
    }
    if (!oc) {
        if (cfg->noutput_cfgs >= CFG_MAX_OUTPUT_CFGS) {
            return luaL_error(L, "too many output() calls (max %d)", CFG_MAX_OUTPUT_CFGS);
        }
        oc = &cfg->output_cfgs[cfg->noutput_cfgs++];
        *oc = {};
        oc->transform = -1;
        oc->enabled = true;
        strncpy(oc->name, name, sizeof(oc->name) - 1);
    }

    lua_getfield(L, 2, "mode");
    if (lua_isstring(L, -1)) {
        const char* mode = lua_tostring(L, -1);
        float hz = 0.0f;
        int n = sscanf(mode, "%dx%d@%f", &oc->width, &oc->height, &hz);
        if (n < 2 || oc->width <= 0 || oc->height <= 0) {
            return luaL_error(L, "output '%s': bad mode '%s' (want \"WxH\" or \"WxH@Hz\")", name,
                              mode);
        }
        oc->refresh_mhz = static_cast<int>(hz * 1000.0f + 0.5f);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "position");
    if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, 1);
        lua_rawgeti(L, -2, 2);
        if (!lua_isnumber(L, -2) || !lua_isnumber(L, -1)) {
            return luaL_error(L, "output '%s': position must be {x, y}", name);
        }
        oc->x = static_cast<int>(lua_tointeger(L, -2));
        oc->y = static_cast<int>(lua_tointeger(L, -1));
        oc->has_position = true;
        lua_pop(L, 2);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "scale");
    if (lua_isnumber(L, -1)) {
        oc->scale = static_cast<float>(lua_tonumber(L, -1));
        if (oc->scale <= 0.0f) {
            return luaL_error(L, "output '%s': scale must be > 0", name);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "transform");
    if (lua_isstring(L, -1)) {
        oc->transform = parse_transform(lua_tostring(L, -1));
        if (oc->transform < 0) {
            return luaL_error(L, "output '%s': unknown transform '%s'", name, lua_tostring(L, -1));
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "enabled");
    if (lua_isboolean(L, -1)) {
        oc->enabled = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);

    return 0;
}

void config::read_color(lua_State* L, const char* name, float out[4]) {
    lua_getglobal(L, name);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    for (int i = 0; i < 4; i++) {
        lua_rawgeti(L, -1, i + 1);
        if (lua_isnumber(L, -1)) {
            out[i] = static_cast<float>(lua_tonumber(L, -1));
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

void config::read_bool(lua_State* L, const char* name, bool* out) {
    lua_getglobal(L, name);
    if (lua_isboolean(L, -1)) {
        *out = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
}

void config::read_int(lua_State* L, const char* name, int* out) {
    lua_getglobal(L, name);
    if (lua_isinteger(L, -1)) {
        *out = static_cast<int>(lua_tointeger(L, -1));
    } else if (lua_isnumber(L, -1)) {
        *out = static_cast<int>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);
}

void config::read_float(lua_State* L, const char* name, float* out) {
    lua_getglobal(L, name);
    if (lua_isnumber(L, -1)) {
        *out = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);
}

void config::read_string(lua_State* L, const char* name, char* out, const size_t len) {
    lua_getglobal(L, name);
    if (lua_isstring(L, -1)) {
        strncpy(out, lua_tostring(L, -1), len - 1);
        out[len - 1] = '\0';
    }
    lua_pop(L, 1);
}

void config::set_defaults() {
    nbinds = 0;
    noutput_cfgs = 0;

    xkb_layout[0] = '\0';
    xkb_variant[0] = '\0';
    xkb_options[0] = '\0';
    repeat_rate = 25;
    repeat_delay = 600;
    tap_to_click = false;
    natural_scroll = false;
    pointer_accel = 0.0f;
    accel_profile[0] = '\0';

    xwayland = false;

    color_title_active[0] = 0.24f;
    color_title_active[1] = 0.24f;
    color_title_active[2] = 0.24f;
    color_title_active[3] = 1.0f;

    color_title_inactive[0] = 0.14f;
    color_title_inactive[1] = 0.14f;
    color_title_inactive[2] = 0.14f;
    color_title_inactive[3] = 1.0f;

    color_border[0] = 0.20f;
    color_border[1] = 0.20f;
    color_border[2] = 0.20f;
    color_border[3] = 1.0f;

    color_close_active[0] = 0.85f;
    color_close_active[1] = 0.08f;
    color_close_active[2] = 0.08f;
    color_close_active[3] = 1.0f;

    color_close_inactive[0] = 0.45f;
    color_close_inactive[1] = 0.06f;
    color_close_inactive[2] = 0.06f;
    color_close_inactive[3] = 1.0f;

    color_button[0] = 0.38f;
    color_button[1] = 0.38f;
    color_button[2] = 0.38f;
    color_button[3] = 1.0f;

    color_button_inactive[0] = 0.32f;
    color_button_inactive[1] = 0.32f;
    color_button_inactive[2] = 0.32f;
    color_button_inactive[3] = 1.0f;

    color_invisible[0] = 0.0f;
    color_invisible[1] = 0.0f;
    color_invisible[2] = 0.0f;
    color_invisible[3] = 0.0f;

    color_title_text[0] = 0.88f;
    color_title_text[1] = 0.88f;
    color_title_text[2] = 0.88f;
    color_title_text[3] = 1.0f;
    show_title_text = true;

    title_h = 20;
    border_w = 3;
    corner_size = 8;
    close_button_w = 40;
    maximize_button_w = 20;
    minimize_button_w = 20;

    taskbar_h = 24;
    taskbar_all_outputs = false;
    taskbar_button_w = 200;
    taskbar_button_pad = 2;

    strncpy(battery_path, "auto", sizeof(battery_path));
#ifdef __linux__
    DIR* dir = opendir("/sys/class/power_supply");
    if (dir) {
        dirent* ent;
        while ((ent = readdir(dir))) {
            if (strncmp(ent->d_name, "BAT", 3) == 0) {
                snprintf(battery_path, sizeof(battery_path), "/sys/class/power_supply/%s",
                         ent->d_name);
                break;
            }
        }
        closedir(dir);
    }
#endif

    strncpy(backlight_path, "auto", sizeof(backlight_path));
#ifdef __linux__
    dir = opendir("/sys/class/backlight");
    if (dir) {
        dirent* ent;
        while ((ent = readdir(dir))) {
            if (ent->d_name[0] != '.') {
                snprintf(backlight_path, sizeof(backlight_path), "/sys/class/backlight/%s",
                         ent->d_name);
                break;
            }
        }
        closedir(dir);
    }
#endif

    color_taskbar_bg[0] = 0.08f;
    color_taskbar_bg[1] = 0.08f;
    color_taskbar_bg[2] = 0.08f;
    color_taskbar_bg[3] = 1.0f;

    color_task_normal[0] = 0.18f;
    color_task_normal[1] = 0.18f;
    color_task_normal[2] = 0.18f;
    color_task_normal[3] = 1.0f;

    color_task_active[0] = 0.30f;
    color_task_active[1] = 0.30f;
    color_task_active[2] = 0.30f;
    color_task_active[3] = 1.0f;

    color_task_minimized[0] = 0.12f;
    color_task_minimized[1] = 0.12f;
    color_task_minimized[2] = 0.12f;
    color_task_minimized[3] = 1.0f;

    color_task_urgent[0] = 0.65f;
    color_task_urgent[1] = 0.08f;
    color_task_urgent[2] = 0.08f;
    color_task_urgent[3] = 1.0f;

    color_task_text[0] = 0.88f;
    color_task_text[1] = 0.88f;
    color_task_text[2] = 0.88f;
    color_task_text[3] = 1.0f;
}

bool config::load(const char* path) {
    if (access(path, R_OK) != 0) {
        return true;
    }

    lua_State* L = luaL_newstate();
    if (!L) {
        return false;
    }
    luaL_openlibs(L);

    lua_pushlightuserdata(L, this);
    lua_setfield(L, LUA_REGISTRYINDEX, "steppewm_cfg");

    lua_pushcfunction(L, lua_exec);
    lua_setglobal(L, "exec");

    lua_pushcfunction(L, lua_bind);
    lua_setglobal(L, "bind");

    lua_pushcfunction(L, lua_output);
    lua_setglobal(L, "output");

    if (luaL_dofile(L, path) != LUA_OK) {
        fprintf(stderr, "steppewm: config error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return false;
    }

    read_string(L, "keyboard_layout", xkb_layout, sizeof(xkb_layout));
    read_string(L, "keyboard_variant", xkb_variant, sizeof(xkb_variant));
    read_string(L, "keyboard_options", xkb_options, sizeof(xkb_options));
    read_int(L, "repeat_rate", &repeat_rate);
    read_int(L, "repeat_delay", &repeat_delay);
    read_bool(L, "tap_to_click", &tap_to_click);
    read_bool(L, "natural_scroll", &natural_scroll);
    read_float(L, "pointer_accel", &pointer_accel);
    read_string(L, "accel_profile", accel_profile, sizeof(accel_profile));

    read_bool(L, "xwayland", &xwayland);

    read_color(L, "title_active", color_title_active);
    read_color(L, "title_inactive", color_title_inactive);
    read_color(L, "border_color", color_border);
    read_color(L, "close_active", color_close_active);
    read_color(L, "close_inactive", color_close_inactive);
    read_color(L, "button_color", color_button);
    read_color(L, "button_inactive", color_button_inactive);

    read_color(L, "title_text", color_title_text);
    read_bool(L, "show_title_text", &show_title_text);

    read_int(L, "title_height", &title_h);
    read_int(L, "border_width", &border_w);
    read_int(L, "corner_size", &corner_size);
    read_int(L, "close_button_width", &close_button_w);
    read_int(L, "maximize_button_width", &maximize_button_w);
    read_int(L, "minimize_button_width", &minimize_button_w);

    read_int(L, "taskbar_height", &taskbar_h);
    read_int(L, "taskbar_button_width", &taskbar_button_w);
    read_int(L, "taskbar_button_pad", &taskbar_button_pad);
    read_bool(L, "taskbar_all_outputs", &taskbar_all_outputs);
    read_color(L, "taskbar_bg", color_taskbar_bg);
    read_color(L, "task_normal", color_task_normal);
    read_color(L, "task_active", color_task_active);
    read_color(L, "task_minimized", color_task_minimized);
    read_color(L, "task_urgent", color_task_urgent);
    read_color(L, "task_text", color_task_text);
    read_string(L, "battery_path", battery_path, sizeof(battery_path));
    read_string(L, "backlight_path", backlight_path, sizeof(backlight_path));

    lua_close(L);
    return true;
}

const output_config* config::find_output(const char* name) const {
    for (int i = 0; i < noutput_cfgs; i++) {
        if (strcmp(output_cfgs[i].name, name) == 0) {
            return &output_cfgs[i];
        }
    }
    return nullptr;
}

void config::run_execs() {
    for (int i = 0; i < nexecs; i++) {
        // don't run those we already ran
        bool already = false;
        for (int j = 0; j < nran; j++) {
            if (strcmp(execs[i], ran_execs[j]) == 0) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }

        if (nran < CFG_MAX_EXECS) {
            strncpy(ran_execs[nran++], execs[i], CFG_MAX_CMD - 1);
        }

        if (pid_t pid = fork(); pid == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", execs[i], static_cast<char*>(nullptr));
            _exit(1);
        }
    }
}

void config::reload(server* s) {
    config* cfg = &s->cfg;

    cfg->set_defaults();
    cfg->nexecs = 0;

    cfg->load(s->config_path);

    cfg->run_execs();

    // re-apply keymap, repeat info, and libinput settings to all input devices
    server::input_reconfigure(s);

    // re-apply decoration colors/sizes and content layout to open windows
    view::reconfigure_all(s);

    // re-apply output modes/positions and taskbar geometry
    output::reconfigure_all(s);
}
