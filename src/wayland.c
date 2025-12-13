#include "wayland.h"
#include "opengl.h"
#include "utils.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define UNUSED(x) (void)(x)

static void frame_done(void *data, struct wl_callback *cb, uint32_t time);
static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h);
static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface);
static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version);
static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name);

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps);
static void seat_handle_name(void *data, struct wl_seat *seat, const char *name);

static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy);
static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface);
static void pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                                  wl_fixed_t sx, wl_fixed_t sy);
static void pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                                  uint32_t time, uint32_t button, uint32_t button_state);
static void pointer_handle_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                                uint32_t axis, wl_fixed_t value);
static void pointer_handle_frame(void *data, struct wl_pointer *pointer);
static void pointer_handle_axis_source(void *data, struct wl_pointer *pointer,
                                       uint32_t axis_source);
static void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                                     uint32_t axis);
static void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
                                         int32_t discrete);

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
    UNUSED(time);
    struct glwall_output *output = data;
    LOG_DEBUG(output->state, "Wayland event: frame_done callback invoked for output %u",
              output->output_name);
    wl_callback_destroy(cb);
    render_frame(output);
}

const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h) {
    struct glwall_output *output = data;
    struct glwall_state *state = output->state;
    LOG_DEBUG(
        state,
        "Wayland event: layer_surface_configure for output %u (serial: %u, dimensions: %u x %u)",
        output->output_name, serial, w, h);

    if (surface == output->layer_surface) {

        output->width_px = w;
        output->height_px = h;
        output->configured = true;

        if (output->wl_egl_window) {
            LOG_DEBUG(state, "EGL subsystem: EGL window resize operation initiated for output %u",
                      output->output_name);
            wl_egl_window_resize(output->wl_egl_window, w, h, 0, 0);
        }

        zwlr_layer_surface_v1_ack_configure(surface, serial);
        LOG_DEBUG(state, "Wayland protocol: configure acknowledgment sent for output %u",
                  output->output_name);

        if (state->shader_program != 0) {
            LOG_DEBUG(
                state,
                "Render cycle: re-render triggered for output %u (OpenGL ready, configure event)",
                output->output_name);
            render_frame(output);
        }
    } else if (surface == output->overlay_layer_surface) {

        zwlr_layer_surface_v1_ack_configure(surface, serial);

        if (!output->overlay_surface || !state->compositor) {
            return;
        }

        struct wl_region *region = wl_compositor_create_region(state->compositor);
        if (!region)
            return;

        if (state->mouse_overlay_mode == GLWALL_MOUSE_OVERLAY_EDGE) {
            int edge_h = state->mouse_overlay_edge_height_px;
            if (edge_h > 0) {

                int y = (int)h - edge_h;
                if (y < 0)
                    y = 0;
                wl_region_add(region, 0, y, (int)w, edge_h);
            }
        } else if (state->mouse_overlay_mode == GLWALL_MOUSE_OVERLAY_FULL) {
            wl_region_add(region, 0, 0, (int)w, (int)h);
        }

        wl_surface_set_input_region(output->overlay_surface, region);
        wl_region_destroy(region);
        wl_surface_commit(output->overlay_surface);

        LOG_DEBUG(state, "Input subsystem: mouse overlay region configured for output %u",
                  output->output_name);
    }
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
    UNUSED(surface);
    struct glwall_output *output = data;
    LOG_DEBUG(output->state, "Wayland event: layer_surface_closed callback invoked for output %u",
              output->output_name);
    output->state->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version) {
    UNUSED(version);
    struct glwall_state *state = data;
    LOG_DEBUG(state,
              "Wayland event: registry_handle_global callback invoked (name: %u, interface: %s, "
              "version: %u)",
              name, interface, version);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        LOG_DEBUG(state, "%s", "Wayland protocol: binding wl_compositor");
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        LOG_DEBUG(state, "Wayland protocol: binding wl_seat (name: %u)", name);
        state->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        if (!state->seat) {
            LOG_ERROR("%s", "Wayland protocol error: unable to bind wl_seat");
            return;
        }
        wl_seat_add_listener(state->seat, &seat_listener, state);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {

        uint32_t interface_version = (uint32_t)zwlr_layer_shell_v1_interface.version;
        uint32_t bind_version = (version < interface_version) ? version : interface_version;
        LOG_DEBUG(state,
                  "Binding zwlr_layer_shell_v1 (offered v%u, client supports v%u, binding v%u)",
                  version, zwlr_layer_shell_v1_interface.version, bind_version);
        state->layer_shell =
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, bind_version);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        LOG_DEBUG(state, "Wayland protocol: binding wl_output (name: %u)", name);
        struct glwall_output *output = calloc(1, sizeof(struct glwall_output));
        if (!output) {
            LOG_ERROR("%s", "Memory allocation failed: insufficient memory for output structure");
            return;
        }
        output->state = state;
        output->output_name = name;
        output->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        output->next = state->outputs;
        state->outputs = output;
        LOG_INFO("Display subsystem: output %u detected", name);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {

    UNUSED(data);
    UNUSED(registry);
    UNUSED(name);
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct glwall_state *state = data;
    UNUSED(seat);

    bool has_pointer = caps & WL_SEAT_CAPABILITY_POINTER;
    LOG_DEBUG(state, "Wayland event: seat capabilities changed (pointer: %s)",
              has_pointer ? "enabled" : "disabled");

    if (has_pointer && !state->pointer) {
        state->pointer = wl_seat_get_pointer(state->seat);
        if (!state->pointer) {
            LOG_ERROR("%s", "Wayland protocol error: unable to obtain wl_pointer from seat");
            return;
        }
        wl_pointer_add_listener(state->pointer, &pointer_listener, state);
        state->pointer_output = NULL;
        state->pointer_x = state->pointer_y = 0.0;
        state->pointer_down = false;
        state->pointer_down_x = state->pointer_down_y = 0.0;
        LOG_DEBUG(state, "%s", "Input subsystem: wl_pointer created for seat");
    } else if (!has_pointer && state->pointer) {
        wl_pointer_destroy(state->pointer);
        state->pointer = NULL;
        state->pointer_output = NULL;
        LOG_DEBUG(state, "%s", "Input subsystem: wl_pointer destroyed (capability removed)");
    }
}

static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
    struct glwall_state *state = data;
    UNUSED(seat);
    LOG_DEBUG(state, "Wayland event: seat name assigned (name: %s)", name);
}

static struct glwall_output *find_output_for_surface(struct glwall_state *state,
                                                     struct wl_surface *surface) {
    for (struct glwall_output *output = state->outputs; output; output = output->next) {
        if (output->wl_surface == surface)
            return output;
    }
    return NULL;
}

static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    UNUSED(pointer);
    UNUSED(serial);
    struct glwall_state *state = data;

    state->pointer_output = find_output_for_surface(state, surface);
    state->pointer_x = wl_fixed_to_double(sx);
    state->pointer_y = wl_fixed_to_double(sy);

    if (state->pointer_output) {
        LOG_DEBUG(state, "Input event: pointer_enter on output %u (position: %.1f, %.1f)",
                  state->pointer_output->output_name, state->pointer_x, state->pointer_y);
    } else {
        LOG_DEBUG(state, "%s", "Input event: pointer_enter on unknown surface");
    }
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface) {
    UNUSED(pointer);
    UNUSED(serial);
    struct glwall_state *state = data;
    UNUSED(surface);

    LOG_DEBUG(state, "%s", "Input event: pointer_leave callback invoked");
    state->pointer_output = NULL;
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                                  wl_fixed_t sx, wl_fixed_t sy) {
    UNUSED(pointer);
    UNUSED(time);
    struct glwall_state *state = data;

    state->pointer_x = wl_fixed_to_double(sx);
    state->pointer_y = wl_fixed_to_double(sy);
}

static void pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                                  uint32_t time, uint32_t button, uint32_t button_state) {
    UNUSED(pointer);
    UNUSED(serial);
    UNUSED(time);
    UNUSED(button);
    struct glwall_state *state = data;

    if (button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
        state->pointer_down = true;
        state->pointer_down_x = state->pointer_x;
        state->pointer_down_y = state->pointer_y;
        LOG_DEBUG(state, "Input event: pointer_button press (position: %.1f, %.1f)",
                  state->pointer_down_x, state->pointer_down_y);
    } else if (button_state == WL_POINTER_BUTTON_STATE_RELEASED) {
        state->pointer_down = false;
        LOG_DEBUG(state, "%s", "Input event: pointer_button release callback invoked");
    }
}

static void pointer_handle_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                                uint32_t axis, wl_fixed_t value) {
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(time);
    UNUSED(axis);
    UNUSED(value);
}

static void pointer_handle_frame(void *data, struct wl_pointer *pointer) {
    UNUSED(data);
    UNUSED(pointer);
}

static void pointer_handle_axis_source(void *data, struct wl_pointer *pointer,
                                       uint32_t axis_source) {
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(axis_source);
}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                                     uint32_t axis) {
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(time);
    UNUSED(axis);
}

static void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
                                         int32_t discrete) {
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(axis);
    UNUSED(discrete);
}

bool init_wayland(struct glwall_state *state) {
    assert(state != NULL);

    state->display = wl_display_connect(NULL);
    if (!state->display) {
        LOG_ERROR("%s", "Wayland subsystem error: unable to connect to Wayland display");
        return false;
    }
    state->registry = wl_display_get_registry(state->display);

    wl_registry_add_listener(state->registry, &registry_listener, state);
    wl_display_roundtrip(state->display);

    if (!state->compositor || !state->layer_shell) {
        LOG_ERROR("%s", "Wayland subsystem error: required components unavailable (wl_compositor "
                        "or wlr_layer_shell)");
        return false;
    }
    return true;
}

void create_layer_surfaces(struct glwall_state *state) {
    assert(state != NULL);

    if (!state->outputs) {
        LOG_ERROR("%s", "Display subsystem error: no Wayland outputs detected");
        state->running = false;
        return;
    }
    for (struct glwall_output *output = state->outputs; output; output = output->next) {

        output->wl_surface = wl_compositor_create_surface(state->compositor);
        output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            state->layer_shell, output->wl_surface, output->wl_output, state->layer, "glwall");

        zwlr_layer_surface_v1_add_listener(output->layer_surface, &layer_surface_listener, output);

        output->wl_egl_window = wl_egl_window_create(output->wl_surface, 1, 1);
        if (!output->wl_egl_window) {
            LOG_ERROR("EGL subsystem error: unable to create EGL window for output %u",
                      output->output_name);

            return;
        }

        zwlr_layer_surface_v1_set_anchor(
            output->layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            output->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
        zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
        wl_surface_commit(output->wl_surface);

        if (state->mouse_overlay_mode != GLWALL_MOUSE_OVERLAY_NONE) {
            output->overlay_surface = wl_compositor_create_surface(state->compositor);
            if (!output->overlay_surface) {
                LOG_ERROR("Display subsystem error: unable to create overlay surface for output %u",
                          output->output_name);
                return;
            }
            output->overlay_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
                state->layer_shell, output->overlay_surface, output->wl_output,
                ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "glwall-mouse-overlay");
            if (!output->overlay_layer_surface) {
                LOG_ERROR(
                    "Display subsystem error: unable to create overlay layer surface for output %u",
                    output->output_name);
                return;
            }

            zwlr_layer_surface_v1_add_listener(output->overlay_layer_surface,
                                               &layer_surface_listener, output);

            zwlr_layer_surface_v1_set_anchor(
                output->overlay_layer_surface,
                ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
            zwlr_layer_surface_v1_set_keyboard_interactivity(
                output->overlay_layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
            zwlr_layer_surface_v1_set_exclusive_zone(output->overlay_layer_surface, 0);
            wl_surface_commit(output->overlay_surface);
        }
    }
    wl_display_roundtrip(state->display);
}

void start_rendering(struct glwall_state *state) {
    LOG_INFO("%s", "Render cycle: initialization complete, rendering commenced");
    for (struct glwall_output *output = state->outputs; output; output = output->next) {
        if (output->configured) {

            render_frame(output);
        }
    }
}

void cleanup_wayland(struct glwall_state *state) {
    struct glwall_output *output = state->outputs;
    while (output) {
        if (output->overlay_layer_surface)
            zwlr_layer_surface_v1_destroy(output->overlay_layer_surface);
        if (output->overlay_surface)
            wl_surface_destroy(output->overlay_surface);
        if (output->layer_surface)
            zwlr_layer_surface_v1_destroy(output->layer_surface);
        if (output->wl_surface)
            wl_surface_destroy(output->wl_surface);
        if (output->wl_output)
            wl_output_destroy(output->wl_output);
        struct glwall_output *next = output->next;
        free(output);
        output = next;
    }
    if (state->layer_shell)
        zwlr_layer_shell_v1_destroy(state->layer_shell);
    if (state->compositor)
        wl_compositor_destroy(state->compositor);
    if (state->registry)
        wl_registry_destroy(state->registry);
    if (state->display)
        wl_display_disconnect(state->display);
}