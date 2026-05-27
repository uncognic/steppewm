#pragma once

#include <wayland-server-core.h>

struct parwm_server;
struct wlr_output;
struct wlr_scene_output;

struct parwm_output {
    struct parwm_server *server;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;

    struct wl_list link;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

void output_new(struct wl_listener *listener, void *data);
