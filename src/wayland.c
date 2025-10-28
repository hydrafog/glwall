/**
 * @file wayland.c
 * @brief Implementation of Wayland compositor integration
 * 
 * This file handles Wayland display connection, registry event handling,
 * output discovery, layer surface creation, and frame callbacks.
 */

#include <string.h>
#include <stdlib.h>
#include "wayland.h"
#include "utils.h"
#include "opengl.h"

// Include the generated protocol header to ensure all definitions are available.
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

// Helper macro to explicitly mark parameters as unused, silencing warnings.
#define UNUSED(x) (void)(x)

// Private Function Declarations

static void frame_done(void *data, struct wl_callback *cb, uint32_t time);
static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface, uint32_t serial, uint32_t w, uint32_t h);
static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface);
static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name);

// Listener Implementations

/**
 * @brief Frame callback handler
 * 
 * Called when the compositor signals that it's time to draw the next frame.
 * Destroys the callback and triggers rendering for the output.
 * 
 * @param data Pointer to glwall_output structure
 * @param cb Callback object to destroy
 * @param time Compositor timestamp (unused)
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
 * Updates the output size, resizes the EGL window, acknowledges the
 * configure event, and triggers a re-render if OpenGL is initialized.
 * 
 * @param data Pointer to glwall_output structure
 * @param surface Layer surface being configured
 * @param serial Serial number for acknowledgment
 * @param w New width
 * @param h New height
 */
static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface, uint32_t serial, uint32_t w, uint32_t h) {
    struct glwall_output *output = data;
    LOG_DEBUG(output->state, "[EVENT] layer_surface_configure for output %u: serial=%u, w=%u, h=%u", output->output_name, serial, w, h);

    output->width = w;
    output->height = h;
    output->configured = true;

    if (output->wl_egl_window) {
        LOG_DEBUG(output->state, "Resizing EGL window for output %u", output->output_name);
        wl_egl_window_resize(output->wl_egl_window, w, h, 0, 0);
    }

    zwlr_layer_surface_v1_ack_configure(surface, serial);
    LOG_DEBUG(output->state, "Acked configure for output %u", output->output_name);

    // A configure event means the compositor has changed the surface
    // properties (like size). We must draw a new frame and commit it to apply
    // this change. If we don't, the compositor may stop sending events for this
    // surface, stalling the render loop.
    // We only do this if OpenGL has been initialized, as configure events can
    // happen before the renderer is ready.
    if (output->state->shader_program != 0) {
        LOG_DEBUG(output->state, "OpenGL is ready, triggering re-render for output %u due to configure event.", output->output_name);
        render_frame(output);
    }
}

/**
 * @brief Layer surface closed event handler
 * 
 * Called when the compositor closes the layer surface. Sets the running
 * flag to false to trigger application shutdown.
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

/**
 * @brief Registry global event handler
 * 
 * Called for each global object advertised by the compositor. Binds to
 * compositor, layer-shell, and output interfaces.
 * 
 * @param data Pointer to glwall_state structure
 * @param registry Wayland registry
 * @param name Global object name
 * @param interface Interface name string
 * @param version Interface version
 */
static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    UNUSED(version);
    struct glwall_state *state = data;
    LOG_DEBUG(state, "[EVENT] registry_handle_global: name=%u, interface=%s, version=%u", name, interface, version);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        LOG_DEBUG(state, "Binding wl_compositor");
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        // Negotiate the protocol version instead of hardcoding '1'.
        // This improves compatibility and can prevent compositor-side bugs.
        // We bind the minimum of the version supported by the compositor and the
        // version supported by our generated headers.
        uint32_t interface_version = (uint32_t)zwlr_layer_shell_v1_interface.version;
        uint32_t bind_version = (version < interface_version) ? version : interface_version;
        LOG_DEBUG(state, "Binding zwlr_layer_shell_v1 (offered v%u, client supports v%u, binding v%u)", version, zwlr_layer_shell_v1_interface.version, bind_version);
        state->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, bind_version);
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
 * Called when a global object is removed. Currently not handled.
 * 
 * @param data Pointer to glwall_state structure (unused)
 * @param registry Wayland registry (unused)
 * @param name Global object name (unused)
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


// Public Function Implementations

/**
 * @brief Initializes Wayland connection and discovers outputs
 * 
 * Connects to the Wayland display, retrieves the registry, adds a listener
 * to discover globals, and performs a roundtrip to process events.
 * 
 * @param state Pointer to global application state
 * @return true on success, false on failure
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
 * For each discovered output, creates a Wayland surface, layer surface,
 * and EGL window. Configures the layer surface as a fullscreen background.
 * 
 * @param state Pointer to global application state
 */
void create_layer_surfaces(struct glwall_state *state) {
    if (!state->outputs) {
        LOG_ERROR("No Wayland outputs found.");
        state->running = false;
        return;
    }
    for (struct glwall_output *output = state->outputs; output; output = output->next) {
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

        zwlr_layer_surface_v1_set_anchor(output->layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_keyboard_interactivity(output->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
        zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
        wl_surface_commit(output->wl_surface);
    }
    wl_display_roundtrip(state->display);
}

/**
 * @brief Starts the rendering loop for all configured outputs
 * 
 * Triggers the initial render_frame() call for each configured output,
 * which begins the frame callback loop.
 * 
 * @param state Pointer to global application state
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
        if (output->layer_surface) zwlr_layer_surface_v1_destroy(output->layer_surface);
        if (output->wl_surface) wl_surface_destroy(output->wl_surface);
        if (output->wl_output) wl_output_destroy(output->wl_output);
        struct glwall_output *next = output->next;
        free(output);
        output = next;
    }
    if (state->layer_shell) zwlr_layer_shell_v1_destroy(state->layer_shell);
    if (state->compositor) wl_compositor_destroy(state->compositor);
    if (state->registry) wl_registry_destroy(state->registry);
    if (state->display) wl_display_disconnect(state->display);
}