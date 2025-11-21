/**
 * @file wayland.c
 * @brief Implementation of Wayland compositor integration
 *
 * This file handles Wayland display connection, registry event handling,
 * output discovery, layer surface creation, and frame callbacks.
 */

#include "wayland.h"
#include "opengl.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

// Include the generated protocol header to ensure all definitions are available.
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

// Helper macro to explicitly mark parameters as unused, silencing warnings.
#define UNUSED(x) (void)(x)

// Private Function Declarations

static void frame_done(void *data, struct wl_callback *cb, uint32_t time);
static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h);
static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface);
static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version);
static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name);

// Seat and pointer input handling (for iMouse)
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

// Listener Implementations

/**
 * @brief Frame callback handler
 *
 * Called when the compositor signals that it's time to draw the next frame.
 * This initiates the rendering cycle for the output and schedules the next
 * frame callback to continue the animation loop.
 *
 * @param data Pointer to glwall_output structure
 * @param cb Callback object to destroy
 * @param time Compositor timestamp (unused in this implementation)
 */
static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
    UNUSED(time);
    struct glwall_output *output = data;
    LOG_DEBUG(output->state, "[EVENT] frame_done for output %u", output->output_name);
    wl_callback_destroy(cb);
    render_frame(output);
}

const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

/**
 * @brief Layer surface configure event handler
 *
 * Called when the compositor configures the layer surface dimensions.
 * For the main background surface, updates output size, resizes the EGL
 * window, and triggers a re-render if OpenGL is ready. For optional
 * overlay surfaces, configures the input region based on mouse overlay mode.
 *
 * @param data Pointer to glwall_output structure
 * @param surface Layer surface being configured
 * @param serial Serial number for acknowledgment
 * @param w New width in pixels
 * @param h New height in pixels
 */
static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h) {
    struct glwall_output *output = data;
    struct glwall_state *state = output->state;
    LOG_DEBUG(state, "[EVENT] layer_surface_configure for output %u: serial=%u, w=%u, h=%u",
              output->output_name, serial, w, h);

    if (surface == output->layer_surface) {
        // Main wallpaper surface
        output->width = w;
        output->height = h;
        output->configured = true;

        if (output->wl_egl_window) {
            LOG_DEBUG(state, "Resizing EGL window for output %u", output->output_name);
            wl_egl_window_resize(output->wl_egl_window, w, h, 0, 0);
        }

        zwlr_layer_surface_v1_ack_configure(surface, serial);
        LOG_DEBUG(state, "Acked configure for output %u", output->output_name);

        // A configure event means the compositor has changed the surface
        // properties (like size). We must draw a new frame and commit it to apply
        // this change. If we don't, the compositor may stop sending events for this
        // surface, stalling the render loop.
        // We only do this if OpenGL has been initialized, as configure events can
        // happen before the renderer is ready.
        if (state->shader_program != 0) {
            LOG_DEBUG(state,
                      "OpenGL is ready, triggering re-render for output %u due to configure event.",
                      output->output_name);
            render_frame(output);
        }
    } else if (surface == output->overlay_layer_surface) {
        // Input-only overlay surface used for mouse overlay modes.
        zwlr_layer_surface_v1_ack_configure(surface, serial);

        if (!output->overlay_surface || !state->compositor) {
            return;
        }

        struct wl_region *region = wl_compositor_create_region(state->compositor);
        if (!region)
            return;

        if (state->mouse_overlay_mode == GLWALL_MOUSE_OVERLAY_EDGE) {
            int edge_h = state->mouse_overlay_edge_height;
            if (edge_h > 0) {
                // Bottom edge strip: x=0..w, y=(h-edge_h)..h
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

        LOG_DEBUG(state, "Configured mouse overlay input region for output %u",
                  output->output_name);
    }
}

/**
 * @brief Layer surface closed event handler
 *
 * Called when the compositor closes the layer surface (e.g., due to
 * compositor shutdown or error). Sets the running flag to false to
 * trigger graceful application shutdown.
 *
 * @param data Pointer to glwall_output structure
 * @param surface Layer surface being closed
 */
static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
    UNUSED(surface);
    struct glwall_output *output = data;
    LOG_DEBUG(output->state, "[EVENT] layer_surface_closed for output %u", output->output_name);
    output->state->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

// Seat listener: sets up pointer capabilities when available.
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

// Pointer listener: tracks pointer position and button state for iMouse.
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

/**
 * @brief Registry global event handler
 *
 * Called for each global object advertised by the compositor. Binds to
 * compositor, layer-shell, seat, and output interfaces. For outputs,
 * creates a new glwall_output structure to track them.
 *
 * @param data Pointer to glwall_state structure
 * @param registry Wayland registry
 * @param name Global object name/identifier
 * @param interface Interface name string (e.g., "wl_compositor")
 * @param version Protocol version advertised by the compositor
 */
static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version) {
    UNUSED(version);
    struct glwall_state *state = data;
    LOG_DEBUG(state, "[EVENT] registry_handle_global: name=%u, interface=%s, version=%u", name,
              interface, version);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        LOG_DEBUG(state, "Binding wl_compositor");
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        LOG_DEBUG(state, "Binding wl_seat %u", name);
        state->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        if (!state->seat) {
            LOG_ERROR("Failed to bind wl_seat");
            return;
        }
        wl_seat_add_listener(state->seat, &seat_listener, state);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        // Negotiate the protocol version instead of hardcoding '1'.
        // This improves compatibility and can prevent compositor-side bugs.
        // We bind the minimum of the version supported by the compositor and the
        // version supported by our generated headers.
        uint32_t interface_version = (uint32_t)zwlr_layer_shell_v1_interface.version;
        uint32_t bind_version = (version < interface_version) ? version : interface_version;
        LOG_DEBUG(state,
                  "Binding zwlr_layer_shell_v1 (offered v%u, client supports v%u, binding v%u)",
                  version, zwlr_layer_shell_v1_interface.version, bind_version);
        state->layer_shell =
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, bind_version);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        LOG_DEBUG(state, "Binding wl_output %u", name);
        struct glwall_output *output = calloc(1, sizeof(struct glwall_output));
        if (!output) {
            LOG_ERROR("Failed to allocate memory for output");
            return;
        }
        output->state = state;
        output->output_name = name;
        output->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        output->next = state->outputs;
        state->outputs = output;
        LOG_INFO("Discovered output %u", name);
    }
}

/**
 * @brief Registry global remove event handler
 *
 * Called when a global object is removed (e.g., when a monitor is
 * disconnected). Currently not handled to keep the implementation simple.
 *
 * @param data Pointer to glwall_state structure (unused)
 * @param registry Wayland registry (unused)
 * @param name Global object name/identifier (unused)
 *
 * @note Runtime output removal is not currently supported.
 */
static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    // Runtime output removal is not handled for simplicity.
    UNUSED(data);
    UNUSED(registry);
    UNUSED(name);
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

/**
 * @brief Seat capabilities event handler
 *
 * Handles changes to seat capabilities and sets up or tears down the
 * wl_pointer accordingly. When pointer capability appears, creates a
 * wl_pointer and attaches the pointer listener; when it disappears,
 * destroys the pointer.
 *
 * @param data Pointer to glwall_state structure
 * @param seat wl_seat associated with the event
 * @param caps Capability bitmask reported by the compositor
 */
// Seat listener implementations
static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct glwall_state *state = data;
    UNUSED(seat);

    bool has_pointer = caps & WL_SEAT_CAPABILITY_POINTER;
    LOG_DEBUG(state, "[EVENT] seat capabilities: pointer=%s", has_pointer ? "yes" : "no");

    if (has_pointer && !state->pointer) {
        state->pointer = wl_seat_get_pointer(state->seat);
        if (!state->pointer) {
            LOG_ERROR("Failed to get wl_pointer from seat");
            return;
        }
        wl_pointer_add_listener(state->pointer, &pointer_listener, state);
        state->pointer_output = NULL;
        state->pointer_x = state->pointer_y = 0.0;
        state->pointer_down = false;
        state->pointer_down_x = state->pointer_down_y = 0.0;
        LOG_DEBUG(state, "Created wl_pointer for seat");
    } else if (!has_pointer && state->pointer) {
        wl_pointer_destroy(state->pointer);
        state->pointer = NULL;
        state->pointer_output = NULL;
        LOG_DEBUG(state, "Destroyed wl_pointer (capability removed)");
    }
}

/**
 * @brief Seat name event handler
 *
 * Logs the human-readable name of the Wayland seat for debugging.
 *
 * @param data Pointer to glwall_state structure
 * @param seat wl_seat associated with the event
 * @param name Seat name string reported by the compositor
 */
static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
    struct glwall_state *state = data;
    UNUSED(seat);
    LOG_DEBUG(state, "[EVENT] seat name: %s", name);
}

/**
 * @brief Find output associated with a Wayland surface
 *
 * Searches the linked list of outputs for the one whose wl_surface matches
 * the given surface pointer.
 *
 * @param state Pointer to global application state
 * @param surface Wayland surface to search for
 * @return Matching glwall_output pointer, or NULL if none is found
 */
// Helper to find output by its wl_surface pointer.
static struct glwall_output *find_output_for_surface(struct glwall_state *state,
                                                     struct wl_surface *surface) {
    for (struct glwall_output *output = state->outputs; output; output = output->next) {
        if (output->wl_surface == surface)
            return output;
    }
    return NULL;
}

/**
 * @brief Pointer enter event handler
 *
 * Records which output the pointer entered and updates pointer coordinates
 * in surface space for iMouse-style shader uniforms.
 *
 * @param data Pointer to glwall_state structure
 * @param pointer wl_pointer associated with the event
 * @param serial Compositor serial for the enter event
 * @param surface Surface the pointer entered
 * @param sx X coordinate in fixed-point surface coordinates
 * @param sy Y coordinate in fixed-point surface coordinates
 */
// Pointer listener implementations
static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    UNUSED(pointer);
    UNUSED(serial);
    struct glwall_state *state = data;

    state->pointer_output = find_output_for_surface(state, surface);
    state->pointer_x = wl_fixed_to_double(sx);
    state->pointer_y = wl_fixed_to_double(sy);

    if (state->pointer_output) {
        LOG_DEBUG(state, "[EVENT] pointer_enter on output %u at (%.1f, %.1f)",
                  state->pointer_output->output_name, state->pointer_x, state->pointer_y);
    } else {
        LOG_DEBUG(state, "[EVENT] pointer_enter on unknown surface");
    }
}

/**
 * @brief Pointer leave event handler
 *
 * Clears the pointer_output when the pointer leaves a surface so shaders
 * no longer receive mouse coordinates for that output.
 *
 * @param data Pointer to glwall_state structure
 * @param pointer wl_pointer associated with the event
 * @param serial Compositor serial for the leave event
 * @param surface Surface the pointer left
 */
static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface) {
    UNUSED(pointer);
    UNUSED(serial);
    struct glwall_state *state = data;
    UNUSED(surface);

    LOG_DEBUG(state, "[EVENT] pointer_leave");
    state->pointer_output = NULL;
}

/**
 * @brief Pointer motion event handler
 *
 * Updates the current pointer coordinates within the active output surface
 * so they can be exposed to shaders via iMouse/mouse uniforms.
 *
 * @param data Pointer to glwall_state structure
 * @param pointer wl_pointer associated with the event
 * @param time Compositor timestamp
 * @param sx X coordinate in fixed-point surface coordinates
 * @param sy Y coordinate in fixed-point surface coordinates
 */
static void pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                                  wl_fixed_t sx, wl_fixed_t sy) {
    UNUSED(pointer);
    UNUSED(time);
    struct glwall_state *state = data;

    state->pointer_x = wl_fixed_to_double(sx);
    state->pointer_y = wl_fixed_to_double(sy);
}

/**
 * @brief Pointer button event handler
 *
 * Tracks primary button press and release for iMouse semantics, storing
 * the click position and a boolean pressed state in the global state.
 *
 * @param data Pointer to glwall_state structure
 * @param pointer wl_pointer associated with the event
 * @param serial Compositor serial for the button event
 * @param time Compositor timestamp
 * @param button Button code (e.g., BTN_LEFT)
 * @param button_state WL_POINTER_BUTTON_STATE_PRESSED or _RELEASED
 */
static void pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                                  uint32_t time, uint32_t button, uint32_t button_state) {
    UNUSED(pointer);
    UNUSED(serial);
    UNUSED(time);
    UNUSED(button);
    struct glwall_state *state = data;

    // Treat any pressed button as primary for iMouse semantics.
    if (button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
        state->pointer_down = true;
        state->pointer_down_x = state->pointer_x;
        state->pointer_down_y = state->pointer_y;
        LOG_DEBUG(state, "[EVENT] pointer_button press at (%.1f, %.1f)", state->pointer_down_x,
                  state->pointer_down_y);
    } else if (button_state == WL_POINTER_BUTTON_STATE_RELEASED) {
        state->pointer_down = false;
        LOG_DEBUG(state, "[EVENT] pointer_button release");
    }
}

/**
 * @brief Pointer axis event handler (scroll)
 *
 * Currently unused; parameters are marked unused to avoid compiler warnings.
 *
 * @param data Pointer to user data (unused)
 * @param pointer wl_pointer associated with the event (unused)
 * @param time Compositor timestamp (unused)
 * @param axis Axis that changed (unused)
 * @param value Axis delta in fixed-point units (unused)
 */
static void pointer_handle_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                                uint32_t axis, wl_fixed_t value) {
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(time);
    UNUSED(axis);
    UNUSED(value);
}

/**
 * @brief Pointer frame event handler
 *
 * Called after a group of pointer events; currently no batching behavior
 * is needed so this is a no-op.
 *
 * @param data Pointer to user data (unused)
 * @param pointer wl_pointer associated with the event (unused)
 */
static void pointer_handle_frame(void *data, struct wl_pointer *pointer) {
    UNUSED(data);
    UNUSED(pointer);
}

/**
 * @brief Pointer axis source event handler
 *
 * Receives information about the source of scroll events (e.g., wheel,
 * finger), but this implementation ignores it.
 *
 * @param data Pointer to user data (unused)
 * @param pointer wl_pointer associated with the event (unused)
 * @param axis_source Axis source code (unused)
 */
static void pointer_handle_axis_source(void *data, struct wl_pointer *pointer,
                                       uint32_t axis_source) {
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(axis_source);
}

/**
 * @brief Pointer axis stop event handler
 *
 * Indicates the end of a scroll gesture; currently ignored.
 *
 * @param data Pointer to user data (unused)
 * @param pointer wl_pointer associated with the event (unused)
 * @param time Compositor timestamp (unused)
 * @param axis Axis that stopped (unused)
 */
static void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                                     uint32_t axis) {
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(time);
    UNUSED(axis);
}

/**
 * @brief Pointer axis discrete event handler
 *
 * Receives discrete scroll steps (e.g., wheel notches). This implementation
 * does not use scroll input, so the parameters are ignored.
 *
 * @param data Pointer to user data (unused)
 * @param pointer wl_pointer associated with the event (unused)
 * @param axis Axis being scrolled (unused)
 * @param discrete Discrete step value (unused)
 */
static void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
                                         int32_t discrete) {
    UNUSED(data);
    UNUSED(pointer);
    UNUSED(axis);
    UNUSED(discrete);
}

// Public Function Implementations

/**
 * @brief Initializes Wayland connection and discovers outputs
 *
 * Connects to the Wayland display, retrieves the registry, adds a listener
 * to discover globals (compositor, layer-shell, outputs, seat), and performs
 * a roundtrip to process all discovery events.
 *
 * @param state Pointer to global application state
 * @return true on success, false on failure (logs errors)
 *
 * @post state->display, state->registry, state->compositor, state->layer_shell initialized
 * @post state->outputs contains linked list of discovered outputs
 */
bool init_wayland(struct glwall_state *state) {
    state->display = wl_display_connect(NULL);
    if (!state->display) {
        LOG_ERROR("Failed to connect to Wayland display.");
        return false;
    }
    state->registry = wl_display_get_registry(state->display);

    wl_registry_add_listener(state->registry, &registry_listener, state);
    wl_display_roundtrip(state->display);

    if (!state->compositor || !state->layer_shell) {
        LOG_ERROR("Wayland compositor or wlr-layer-shell not available.");
        return false;
    }
    return true;
}

/**
 * @brief Creates layer surfaces for all outputs
 *
 * For each discovered output, creates a Wayland surface and layer surface,
 * configures them as a fullscreen background layer, and creates the EGL
 * window. Optionally creates an input-only overlay surface for mouse
 * overlay modes (edge/full).
 *
 * @param state Pointer to global application state
 *
 * @pre init_wayland() must have been called successfully
 * @post Layer surfaces are created and configured for all outputs
 */
void create_layer_surfaces(struct glwall_state *state) {
    if (!state->outputs) {
        LOG_ERROR("No Wayland outputs found.");
        state->running = false;
        return;
    }
    for (struct glwall_output *output = state->outputs; output; output = output->next) {
        // Main wallpaper surface (background layer)
        output->wl_surface = wl_compositor_create_surface(state->compositor);
        output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            state->layer_shell, output->wl_surface, output->wl_output,
            ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "glwall");

        zwlr_layer_surface_v1_add_listener(output->layer_surface, &layer_surface_listener, output);

        // Create the EGL window now so it's available for the first configure event.
        output->wl_egl_window = wl_egl_window_create(output->wl_surface, 1, 1);
        if (!output->wl_egl_window) {
            LOG_ERROR("Failed to create EGL window for output %u", output->output_name);
            // Error: Failed to create EGL window.
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

        // Optional overlay surface for mouse overlay modes.
        if (state->mouse_overlay_mode != GLWALL_MOUSE_OVERLAY_NONE) {
            output->overlay_surface = wl_compositor_create_surface(state->compositor);
            if (!output->overlay_surface) {
                LOG_ERROR("Failed to create overlay surface for output %u", output->output_name);
                return;
            }
            output->overlay_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
                state->layer_shell, output->overlay_surface, output->wl_output,
                ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "glwall-mouse-overlay");
            if (!output->overlay_layer_surface) {
                LOG_ERROR("Failed to create overlay layer surface for output %u",
                          output->output_name);
                return;
            }

            zwlr_layer_surface_v1_add_listener(output->overlay_layer_surface,
                                               &layer_surface_listener, output);

            // Anchor overlay to the full output, but restrict input using regions
            // in the configure handler based on mouse_overlay_mode.
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

/**
 * @brief Starts the rendering loop for all configured outputs
 *
 * Triggers the initial render_frame() call for each configured output,
 * which begins the frame callback loop. This is called after OpenGL is
 * initialized to start the animation.
 *
 * @param state Pointer to global application state
 *
 * @pre create_layer_surfaces() must have been called successfully
 * @pre OpenGL must be initialized
 */
void start_rendering(struct glwall_state *state) {
    LOG_INFO("Kicking off rendering...");
    for (struct glwall_output *output = state->outputs; output; output = output->next) {
        if (output->configured) {
            // By calling render_frame() here, we draw the first frame and also
            // request the next frame callback, which starts the animation loop.
            render_frame(output);
        }
    }
}

/**
 * @brief Cleans up Wayland resources
 *
 * Destroys all layer surfaces, Wayland surfaces, outputs, and disconnects
 * from the display.
 *
 * @param state Pointer to global application state
 */
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