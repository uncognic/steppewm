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

#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

#define CFG_MAX_BINDS 128
#define CFG_MAX_ARG 512
#define CFG_MAX_EXECS 64
#define CFG_MAX_CMD 512

struct steppewm_keybind {
    uint32_t modifiers;
    xkb_keysym_t sym;
    char action[32];
    char arg[CFG_MAX_ARG];
};

struct steppewm_config {
    struct steppewm_keybind binds[CFG_MAX_BINDS];
    int nbinds;

    char execs[CFG_MAX_EXECS][CFG_MAX_CMD];
    int nexecs;

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
};

struct steppewm_server;

void config_defaults(struct steppewm_config *cfg);
bool config_load(struct steppewm_config *cfg, const char *path);
void config_run_execs(struct steppewm_config *cfg);
void config_reload(struct steppewm_server *server);
