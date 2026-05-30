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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>

#include "config.h"

static uint32_t parse_modifiers(const char *str) {
    uint32_t mods = 0;
    char buf[64];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *saveptr;
    char *tok = strtok_r(buf, "+", &saveptr);
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
        tok = strtok_r(NULL, "+", &saveptr);
    }
    return mods;
}

// bind()
static int lua_bind(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "steppewm_cfg");
    struct steppewm_config *cfg = lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (cfg->nbinds >= CFG_MAX_BINDS) {
        return luaL_error(L, "too many keybindings (max %d)", CFG_MAX_BINDS);
    }

    const char *mods_str = luaL_checkstring(L, 1);
    const char *key_str = luaL_checkstring(L, 2);
    const char *action = luaL_checkstring(L, 3);

    xkb_keysym_t sym = xkb_keysym_from_name(key_str, XKB_KEYSYM_CASE_INSENSITIVE);
    if (sym == XKB_KEY_NoSymbol) {
        return luaL_error(L, "unknown key name '%s'", key_str);
    }

    struct steppewm_keybind *b = &cfg->binds[cfg->nbinds++];
    b->modifiers = parse_modifiers(mods_str);
    b->sym = sym;
    strncpy(b->action, action, sizeof(b->action) - 1);

    if (lua_gettop(L) >= 4 && lua_isstring(L, 4)) {
        strncpy(b->arg, lua_tostring(L, 4), sizeof(b->arg) - 1);
    }

    return 0;
}

static void read_color(lua_State *L, const char *name, float out[4]) {
    lua_getglobal(L, name);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    for (int i = 0; i < 4; i++) {
        lua_rawgeti(L, -1, i + 1);
        if (lua_isnumber(L, -1)) {
            out[i] = (float) lua_tonumber(L, -1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

static void read_bool(lua_State *L, const char *name, bool *out) {
    lua_getglobal(L, name);
    if (lua_isboolean(L, -1)) {
        *out = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
}

static void read_int(lua_State *L, const char *name, int *out) {
    lua_getglobal(L, name);
    if (lua_isinteger(L, -1)) {
        *out = (int) lua_tointeger(L, -1);
    } else if (lua_isnumber(L, -1)) {
        *out = (int) lua_tonumber(L, -1);
    }
    lua_pop(L, 1);
}

void config_defaults(struct steppewm_config *cfg) {
    cfg->nbinds = 0;

    cfg->color_title_active[0] = 0.24f;
    cfg->color_title_active[1] = 0.24f;
    cfg->color_title_active[2] = 0.24f;
    cfg->color_title_active[3] = 1.0f;

    cfg->color_title_inactive[0] = 0.14f;
    cfg->color_title_inactive[1] = 0.14f;
    cfg->color_title_inactive[2] = 0.14f;
    cfg->color_title_inactive[3] = 1.0f;

    cfg->color_border[0] = 0.20f;
    cfg->color_border[1] = 0.20f;
    cfg->color_border[2] = 0.20f;
    cfg->color_border[3] = 1.0f;

    cfg->color_close_active[0] = 0.85f;
    cfg->color_close_active[1] = 0.08f;
    cfg->color_close_active[2] = 0.08f;
    cfg->color_close_active[3] = 1.0f;

    cfg->color_close_inactive[0] = 0.45f;
    cfg->color_close_inactive[1] = 0.06f;
    cfg->color_close_inactive[2] = 0.06f;
    cfg->color_close_inactive[3] = 1.0f;

    cfg->color_button[0] = 0.38f;
    cfg->color_button[1] = 0.38f;
    cfg->color_button[2] = 0.38f;
    cfg->color_button[3] = 1.0f;

    cfg->color_button_inactive[0] = 0.32f;
    cfg->color_button_inactive[1] = 0.32f;
    cfg->color_button_inactive[2] = 0.32f;
    cfg->color_button_inactive[3] = 1.0f;

    cfg->color_invisible[0] = 0.0f;
    cfg->color_invisible[1] = 0.0f;
    cfg->color_invisible[2] = 0.0f;
    cfg->color_invisible[3] = 0.0f;

    cfg->title_h = 20;
    cfg->border_w = 3;
    cfg->corner_size = 8;
    cfg->close_button_w = 40;
    cfg->minimize_button_w = 20;

    cfg->taskbar_h = 24;
    cfg->taskbar_all_outputs = false;

    cfg->color_taskbar_bg[0] = 0.08f;
    cfg->color_taskbar_bg[1] = 0.08f;
    cfg->color_taskbar_bg[2] = 0.08f;
    cfg->color_taskbar_bg[3] = 1.0f;

    cfg->color_task_normal[0] = 0.18f;
    cfg->color_task_normal[1] = 0.18f;
    cfg->color_task_normal[2] = 0.18f;
    cfg->color_task_normal[3] = 1.0f;

    cfg->color_task_active[0] = 0.30f;
    cfg->color_task_active[1] = 0.30f;
    cfg->color_task_active[2] = 0.30f;
    cfg->color_task_active[3] = 1.0f;

    cfg->color_task_minimized[0] = 0.12f;
    cfg->color_task_minimized[1] = 0.12f;
    cfg->color_task_minimized[2] = 0.12f;
    cfg->color_task_minimized[3] = 1.0f;

    cfg->color_task_text[0] = 0.88f;
    cfg->color_task_text[1] = 0.88f;
    cfg->color_task_text[2] = 0.88f;
    cfg->color_task_text[3] = 1.0f;
}

bool config_load(struct steppewm_config *cfg, const char *path) {
    if (access(path, R_OK) != 0) {
        return true;
    }

    lua_State *L = luaL_newstate();
    if (!L) {
        return false;
    }
    luaL_openlibs(L);

    lua_pushlightuserdata(L, cfg);
    lua_setfield(L, LUA_REGISTRYINDEX, "steppewm_cfg");

    lua_pushcfunction(L, lua_bind);
    lua_setglobal(L, "bind");

    if (luaL_dofile(L, path) != LUA_OK) {
        fprintf(stderr, "steppewm: config error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return false;
    }

    read_color(L, "title_active", cfg->color_title_active);
    read_color(L, "title_inactive", cfg->color_title_inactive);
    read_color(L, "border_color", cfg->color_border);
    read_color(L, "close_active", cfg->color_close_active);
    read_color(L, "close_inactive", cfg->color_close_inactive);
    read_color(L, "button_color", cfg->color_button);
    read_color(L, "button_inactive", cfg->color_button_inactive);

    read_int(L, "title_height", &cfg->title_h);
    read_int(L, "border_width", &cfg->border_w);
    read_int(L, "corner_size", &cfg->corner_size);
    read_int(L, "close_button_width", &cfg->close_button_w);
    read_int(L, "minimize_button_width", &cfg->minimize_button_w);

    read_int(L, "taskbar_height", &cfg->taskbar_h);
    read_bool(L, "taskbar_all_outputs", &cfg->taskbar_all_outputs);
    read_color(L, "taskbar_bg", cfg->color_taskbar_bg);
    read_color(L, "task_normal", cfg->color_task_normal);
    read_color(L, "task_active", cfg->color_task_active);
    read_color(L, "task_minimized", cfg->color_task_minimized);
    read_color(L, "task_text", cfg->color_task_text);

    lua_close(L);
    return true;
}
