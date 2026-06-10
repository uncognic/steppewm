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

#include <cstdlib>
#include <ctime>

#include "layer.hxx"
#include "lock.hxx"
#include "output.hxx"
#include "server.hxx"
#include "taskbar.hxx"
#include "view.hxx"

using namespace steppewm;

// called on output frame events
void output::on_frame(struct wl_listener* listener, void* data) {
    (void) data;
    output* out = wl_container_of(listener, out, frame);
    struct wlr_scene_output* scene_output =
        wlr_scene_get_scene_output(out->srv->scene, out->wlr_output);
    if (!scene_output) {
        // output is disabled by config and has no scene output
        return;
    }

    wlr_scene_output_commit(scene_output, nullptr);

    struct timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

void output::on_request_state(struct wl_listener* listener, void* data) {
    output* out = wl_container_of(listener, out, request_state);
    const auto* event = static_cast<const struct wlr_output_event_request_state*>(data);
    wlr_output_commit_state(out->wlr_output, event->state);
}

// clean up output
void output::on_destroy(struct wl_listener* listener, void* data) {
    (void) data;
    output* out = wl_container_of(listener, out, destroy);

    // remove taskbar for listener (output)
    if (out->taskbar) {
        delete out->taskbar;
        out->taskbar = nullptr;
    }

    // destroy layer surfaces (sends closed to clients)
    layer_surface *ls, *tmp;
    wl_list_for_each_safe(ls, tmp, &out->layer_surfaces, link) {
        wlr_layer_surface_v1_destroy(ls->wlr_layer_surface);
    }

    // destroy layer trees
    for (auto& layer_tree : out->layer_trees) {
        if (layer_tree) {
            wlr_scene_node_destroy(&layer_tree->node);
            layer_tree = nullptr;
        }
    }

    wl_list_remove(&out->frame.link);
    wl_list_remove(&out->request_state.link);
    wl_list_remove(&out->destroy.link);
    wl_list_remove(&out->link);

    server* s = out->srv;
    delete out;
    broadcast_output_config(s);
}

// update geometry of taskbar and layer surfaces for each output
void output::on_layout_change(struct wl_listener* listener, void* data) {
    (void) data;
    server* s = wl_container_of(listener, s, output_layout_change);
    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        struct wlr_box box;
        wlr_output_layout_get_box(s->output_layout, out->wlr_output, &box);
        if (box.width <= 0) {
            continue;
        }

        // reposition layer trees to the output's new global origin
        for (const auto& layer_tree : out->layer_trees) {
            if (layer_tree) {
                wlr_scene_node_set_position(&layer_tree->node, box.x, box.y);
            }
        }

        // reconfigure layer surfaces with new dimensions
        layer_surface* ls;
        wl_list_for_each(ls, &out->layer_surfaces, link) {
            ls->configure();
        }

        if (out->taskbar) {
            out->taskbar->update_geometry();
            out->taskbar->raise();
        }
    }

    session_lock::update_geometry(s);
    broadcast_output_config(s);
}

// register a layout change
void output::register_layout_change(server* s) {
    s->output_layout_change.notify = on_layout_change;
    wl_signal_add(&s->output_layout->events.change, &s->output_layout_change);
}

void output::init(server* s) {
    // create output layout
    s->output_layout = wlr_output_layout_create(s->display);
    output::register_layout_change(s);

    // slurp needs xdg-output to enumerate output geometry
    // grim needs screencopy to capture pixels
    wlr_xdg_output_manager_v1_create(s->display, s->output_layout);
    wlr_screencopy_manager_v1_create(s->display);

    // wlr-output-management protocol
    s->output_mgr = wlr_output_manager_v1_create(s->display);
    s->output_mgr_apply.notify = on_output_mgr_apply;
    s->output_mgr_test.notify = on_output_mgr_test;
    wl_signal_add(&s->output_mgr->events.apply, &s->output_mgr_apply);
    wl_signal_add(&s->output_mgr->events.test, &s->output_mgr_test);

    // wlr_output_power protocol
    s->output_power_mgr = wlr_output_power_manager_v1_create(s->display);
    s->output_power_set_mode.notify = output::on_power_set_mode;
    wl_signal_add(&s->output_power_mgr->events.set_mode, &s->output_power_set_mode);

    // wlr_gamma_control protocol
    s->gamma_control_mgr = wlr_gamma_control_manager_v1_create(s->display);
    s->set_gamma.notify = output::on_set_gamma;
    wl_signal_add(&s->gamma_control_mgr->events.set_gamma, &s->set_gamma);

    // create listeners and signals
    wl_list_init(&s->outputs);
    s->new_output.notify = output::on_new;
    wl_signal_add(&s->backend->events.new_output, &s->new_output);
}

// pick the output mode best matching the configured width/height/refresh
struct wlr_output_mode* output::pick_mode(struct wlr_output* wlr_output, const output_config* oc) {
    struct wlr_output_mode *mode, *best = nullptr;
    wl_list_for_each(mode, &wlr_output->modes, link) {
        if (mode->width != oc->width || mode->height != oc->height) {
            continue;
        }
        if (!best) {
            best = mode;
        } else if (oc->refresh_mhz > 0) {
            if (abs(mode->refresh - oc->refresh_mhz) < abs(best->refresh - oc->refresh_mhz)) {
                best = mode;
            }
        } else if (mode->refresh > best->refresh) {
            best = mode;
        }
    }
    return best;
}

bool output::any_taskbar(server* s) {
    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        if (out->taskbar) {
            return true;
        }
    }
    return false;
}

// create a taskbar on this output and populate it with the open views
void output::create_taskbar() {
    taskbar = new steppewm::taskbar(srv, wlr_output);
    view* v;
    wl_list_for_each(v, &srv->views, link) {
        taskbar->view_added(v);
    }
}

// apply the user's output() config
void output::apply_config() {
    server* s = srv;
    const output_config* oc = s->cfg.find_output(wlr_output->name);

    // enabled by default
    const bool enable = !oc || oc->enabled;

    wlr_output_state state{};
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, enable);

    if (enable) {
        struct wlr_output_mode* mode = nullptr;

        // find mode if a resolution was supplied
        if (oc && oc->width > 0) {
            mode = pick_mode(wlr_output, oc);
            if (!mode) {
                wlr_output_state_set_custom_mode(&state, oc->width, oc->height, oc->refresh_mhz);
            }
        } else {
            mode = wlr_output_preferred_mode(wlr_output);
        }
        if (mode) {
            wlr_output_state_set_mode(&state, mode);
        }

        // if a scale was set
        if (oc && oc->scale > 0.0f) {
            wlr_output_state_set_scale(&state, oc->scale);
        }

        // if a transform was set
        if (oc && oc->transform >= 0) {
            wlr_output_state_set_transform(&state, static_cast<wl_output_transform>(oc->transform));
        }
    }

    // if output state commit fai;ls
    if (!wlr_output_commit_state(wlr_output, &state) && enable) {
        wlr_log(WLR_ERROR, "output %s: failed to commit configured state, using preferred mode",
                wlr_output->name);

        // fall back to the preferred mode
        wlr_output_state_finish(&state);
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, true);
        if (wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output)) {
            wlr_output_state_set_mode(&state, mode);
        }
        wlr_output_commit_state(wlr_output, &state);
    }
    wlr_output_state_finish(&state);

    if (!enable) {
        if (taskbar) {
            delete taskbar;
            taskbar = nullptr;
        }
        for (const auto& layer_tree : layer_trees) {
            if (layer_tree) {
                wlr_scene_node_set_enabled(&layer_tree->node, false);
            }
        }

        wlr_output_layout_remove(s->output_layout, wlr_output);
        scene_output = nullptr;
        return;
    }

    for (const auto& layer_tree : layer_trees) {
        if (layer_tree) {
            wlr_scene_node_set_enabled(&layer_tree->node, true);
        }
    }

    wlr_output_layout_output* layout_output;

    if (oc && oc->has_position) {
        layout_output = wlr_output_layout_add(s->output_layout, wlr_output, oc->x, oc->y);
    } else {
        layout_output = wlr_output_layout_add_auto(s->output_layout, wlr_output);
    }

    if (!scene_output) {
        scene_output = wlr_scene_output_create(s->scene, wlr_output);
        wlr_scene_output_layout_add_output(s->scene_layout, layout_output, scene_output);
    }
}

// re-apply output configuration to all outputs
void output::reconfigure_all(server* s) {
    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        out->apply_config();
    }
    ensure_taskbars(s);
}

void output::on_set_gamma(struct wl_listener* listener, void* data) {
    (void) listener;
    const auto* event = static_cast<struct wlr_gamma_control_manager_v1_set_gamma_event*>(data);

    wlr_output_state state{};
    wlr_output_state_init(&state);

    if (!wlr_gamma_control_v1_apply(event->control, &state)) {
        wlr_output_state_finish(&state);
        return;
    }

    if (!wlr_output_commit_state(event->output, &state) && event->control) {
        wlr_gamma_control_v1_send_failed_and_destroy(event->control);
    }
    wlr_output_state_finish(&state);
}

void output::on_power_set_mode(struct wl_listener* listener, void* data) {
    (void) listener;
    const auto* event = static_cast<struct wlr_output_power_v1_set_mode_event*>(data);

    wlr_output_state state{};
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
    wlr_output_commit_state(event->output, &state);
    wlr_output_state_finish(&state);
}

// add new output and set it up
void output::on_new(struct wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, new_output);
    auto* wlr_output = static_cast<struct wlr_output*>(data);

    wlr_output_init_render(wlr_output, s->allocator, s->renderer);

    auto* out = new output();
    out->srv = s;
    out->wlr_output = wlr_output;
    wl_list_init(&out->layer_surfaces);

    out->frame.notify = on_frame;
    wl_signal_add(&wlr_output->events.frame, &out->frame);

    out->request_state.notify = on_request_state;
    wl_signal_add(&wlr_output->events.request_state, &out->request_state);

    out->destroy.notify = on_destroy;
    wl_signal_add(&wlr_output->events.destroy, &out->destroy);

    wl_list_insert(&s->outputs, &out->link);

    // create taskbar before the output enters the layout
    const output_config* oc = s->cfg.find_output(wlr_output->name);

    bool enabled;
    if (oc == nullptr) {
        enabled = true;
    } else {
        enabled = oc->enabled;
    }

    if (enabled && (s->cfg.taskbar_all_outputs || !any_taskbar(s))) {
        out->create_taskbar();
    }

    out->apply_config();

    wlr_log(WLR_INFO, "new output: %s", wlr_output->name);
}

output* output::find_for_wlr_output(const server* s, const struct wlr_output* wlr_out) {
    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        if (out->wlr_output == wlr_out) {
            return out;
        }
    }
    return nullptr;
}

void output::ensure_taskbars(const server* s) {
    output* primary = nullptr;
    output* out;
    wl_list_for_each_reverse(out, &s->outputs, link) {
        if (!out->scene_output) {
            continue;
        }
        if (!primary) {
            primary = out;
        }
        if (out->taskbar) {
            primary = out;
            break;
        }
    }

    wl_list_for_each(out, &s->outputs, link) {
        const bool needs = out->scene_output && (s->cfg.taskbar_all_outputs || out == primary);
        if (needs && !out->taskbar) {
            out->create_taskbar();
        } else if (!needs && out->taskbar) {
            delete out->taskbar;
            out->taskbar = nullptr;
        }
        if (out->taskbar) {
            out->taskbar->update_geometry();
            out->taskbar->raise();
        }
    }
}

void output::broadcast_output_config(server* s) {
    wlr_output_configuration_v1* config = wlr_output_configuration_v1_create();

    output* out;
    wl_list_for_each(out, &s->outputs, link) {
        wlr_output_configuration_head_v1* head =
            wlr_output_configuration_head_v1_create(config, out->wlr_output);
        if (!head) {
            wlr_output_configuration_v1_destroy(config);
            return;
        }
        wlr_box box;
        wlr_output_layout_get_box(s->output_layout, out->wlr_output, &box);
        if (box.width > 0) {
            head->state.x = box.x;
            head->state.y = box.y;
        }
    }

    wlr_output_manager_v1_set_configuration(s->output_mgr, config);
}

void output::on_output_mgr_test(wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, output_mgr_test);
    auto* config = static_cast<struct wlr_output_configuration_v1*>(data);

    size_t states_len;
    wlr_backend_output_state* states = wlr_output_configuration_v1_build_state(config, &states_len);
    if (!states) {
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        return;
    }

    const bool ok = wlr_backend_test(s->backend, states, states_len);
    free(states);

    if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }
    wlr_output_configuration_v1_destroy(config);
}

void output::on_output_mgr_apply(wl_listener* listener, void* data) {
    server* s = wl_container_of(listener, s, output_mgr_apply);
    auto* config = static_cast<struct wlr_output_configuration_v1*>(data);

    size_t states_len;
    wlr_backend_output_state* states = wlr_output_configuration_v1_build_state(config, &states_len);
    if (!states) {
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        return;
    }

    if (!wlr_backend_commit(s->backend, states, states_len)) {
        free(states);
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        return;
    }
    free(states);

    // apply layout positions
    wlr_output_configuration_head_v1* head;
    wl_list_for_each(head, &config->heads, link) {
        output* out = find_for_wlr_output(s, head->state.output);
        if (!out) {
            continue;
        }

        if (head->state.enabled) {
            for (const auto& lt : out->layer_trees) {
                if (lt) {
                    wlr_scene_node_set_enabled(&lt->node, true);
                }
            }
            wlr_output_layout_output* lo = wlr_output_layout_add(s->output_layout, out->wlr_output,
                                                                 head->state.x, head->state.y);
            if (!out->scene_output) {
                out->scene_output = wlr_scene_output_create(s->scene, out->wlr_output);
                wlr_scene_output_layout_add_output(s->scene_layout, lo, out->scene_output);
            }
        } else {
            if (out->taskbar) {
                delete out->taskbar;
                out->taskbar = nullptr;
            }
            for (const auto& lt : out->layer_trees) {
                if (lt) {
                    wlr_scene_node_set_enabled(&lt->node, false);
                }
            }
            wlr_output_layout_remove(s->output_layout, out->wlr_output);
            out->scene_output = nullptr;
        }
    }

    ensure_taskbars(s);

    wlr_output_configuration_v1_send_succeeded(config);
    wlr_output_configuration_v1_destroy(config);
}
