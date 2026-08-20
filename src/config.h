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

#include <cstddef>
#include <cstdint>
#include <xkbcommon/xkbcommon.h>

constexpr int CFG_MAX_BINDS = 128;
constexpr int CFG_MAX_SWITCHBINDS = 16;
constexpr int CFG_MAX_ARG = 512;
constexpr int CFG_MAX_EXECS = 64;
constexpr int CFG_MAX_CMD = 512;
constexpr int CFG_MAX_OUTPUT_CFGS = 16;
constexpr int CFG_MAX_PINS = 32;

struct lua_State;

namespace steppewm {

class server;

inline constexpr int num_workspaces = 9;

class keybind {
  public:
    uint32_t modifiers;
    xkb_keysym_t sym;
    char action[32];
    char arg[CFG_MAX_ARG];
};

enum class taskbar_pos : uint8_t { bottom, top };
enum class switch_type : uint8_t { lid, tablet_mode };
enum class switch_state : uint8_t { on, off };

class switchbind {
  public:
    switch_type type;
    switch_state state;
    char action[32];
    char arg[CFG_MAX_ARG];
};

class pinned_app {
  public:
    char app_id[128];
    char command[CFG_MAX_CMD];
    char icon_path[512];
};

class output_config {
  public:
    char name[64];
    int width, height; // 0 = use preferred mode
    int refresh_mhz;   // 0 = highest refresh at WxH
    bool has_position;
    int x, y;
    float scale;   // 0 = unset
    int transform; // -1 = unset
    bool enabled;
};

class config {
  public:
    keybind binds[CFG_MAX_BINDS];
    int nbinds;

    switchbind switchbinds[CFG_MAX_SWITCHBINDS];
    int nswitchbinds;

    char execs[CFG_MAX_EXECS][CFG_MAX_CMD];
    int nexecs;

    char ran_execs[CFG_MAX_EXECS][CFG_MAX_CMD];
    int nran;

    output_config output_cfgs[CFG_MAX_OUTPUT_CFGS];
    int noutput_cfgs;

    // keyboard
    char xkb_layout[64];
    char xkb_variant[64];
    char xkb_options[128];
    int repeat_rate;
    int repeat_delay;

    // pointer
    bool tap_to_click;
    bool natural_scroll;
    float pointer_accel;    // [-1.0, 1.0]
    char accel_profile[16]; // "flat" or "adaptive"

    // xwayland
    bool xwayland;

    float color_title_active[4];
    float color_title_inactive[4];
    float color_border[4];
    float color_border_active[4];
    float color_border_inactive[4];
    float color_close_active[4];
    float color_close_inactive[4];
    float color_button[4];
    float color_button_inactive[4];

    float color_title_text[4];

    float color_maximize_active[4];
    float color_maximize_inactive[4];
    float color_minimize_active[4];
    float color_minimize_inactive[4];
    float color_close_hover[4];
    float color_maximize_hover[4];
    float color_minimize_hover[4];

    int title_h;
    int border_w;
    int corner_size;
    int close_button_w;
    int maximize_button_w;
    int minimize_button_w;

    bool title_gradient;
    char button_style[16];
    char border_style[16];
    char font[64];
    int title_font_size;
    bool center_title_text;
    bool buttons_left;

    int taskbar_h;
    taskbar_pos taskbar_position;
    bool taskbar_all_outputs;
    bool taskbar_all_workspaces;
    float color_taskbar_bg[4];
    float color_taskbar_accent[4];
    float color_task_normal[4];
    float color_task_active[4];
    float color_task_minimized[4];
    float color_task_urgent[4];
    float color_task_text[4];
    float color_tray_bg[4];
    float color_indicator_bg[4];
    int taskbar_button_w;
    int taskbar_button_pad;

    char battery_path[128];
    char backlight_path[128];
    char brightness_scroll_up[CFG_MAX_CMD];
    char brightness_scroll_down[CFG_MAX_CMD];
    char volume_scroll_up[CFG_MAX_CMD];
    char volume_scroll_down[CFG_MAX_CMD];
    char volume_middle_button[CFG_MAX_CMD];

    char theme_path[512];
    char theme_name[128];

    pinned_app pins[CFG_MAX_PINS];
    int npins;

    void set_defaults();
    bool load(const char* path);
    void run_execs();
    const output_config* find_output(const char* name) const;
    static void reload(server* s);

  private:
    static uint32_t parse_modifiers(const char* str);
    static int parse_transform(const char* str);
    static int lua_exec(struct lua_State* L);
    static int lua_bind(struct lua_State* L);
    static int lua_bindswitch(struct lua_State* L);
    static int lua_output(struct lua_State* L);
    static int lua_pin(struct lua_State* L);

    void read_visual(struct lua_State *L);

    void load_theme_lua();

    static void read_color(struct lua_State* L, const char* name, float out[4]);
    static void read_bool(struct lua_State* L, const char* name, bool* out);
    static void read_int(struct lua_State* L, const char* name, int* out);
    static void read_float(struct lua_State* L, const char* name, float* out);
    static void read_string(struct lua_State* L, const char* name, char* out, size_t len);
};

} // namespace steppewm
