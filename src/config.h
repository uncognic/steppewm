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

#include <cstdint>
#include <xkbcommon/xkbcommon.h>

#define CFG_MAX_BINDS 128
#define CFG_MAX_ARG 512
#define CFG_MAX_EXECS 64
#define CFG_MAX_CMD 512

namespace steppewm {

inline constexpr int num_workspaces = 9;

struct keybind {
    uint32_t modifiers;
    xkb_keysym_t sym;
    char action[32];
    char arg[CFG_MAX_ARG];
};

class config {
  public:
    keybind binds[CFG_MAX_BINDS];
    int nbinds;

    char execs[CFG_MAX_EXECS][CFG_MAX_CMD];
    int nexecs;

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

    float color_title_active[4];
    float color_title_inactive[4];
    float color_border[4];
    float color_close_active[4];
    float color_close_inactive[4];
    float color_button[4];
    float color_button_inactive[4];
    float color_invisible[4];

    float color_title_text[4];
    bool show_title_text;

    int title_h;
    int border_w;
    int corner_size;
    int close_button_w;
    int maximize_button_w;
    int minimize_button_w;

    int taskbar_h;
    bool taskbar_all_outputs;
    float color_taskbar_bg[4];
    float color_task_normal[4];
    float color_task_active[4];
    float color_task_minimized[4];
    float color_task_text[4];
    int taskbar_button_w;
    int taskbar_button_pad;

    void set_defaults();
    bool load(const char* path);
    void run_execs();
};

struct server;
void config_reload(server* s);

} // namespace steppewm
