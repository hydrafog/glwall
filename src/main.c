/**
 * @file main.c
 * @brief Main entry point for GLWall
 *
 * This file contains the main() function and event loop for GLWall.
 * It initializes all subsystems (Wayland, EGL, OpenGL), starts the
 * rendering loop, and handles cleanup on exit.
 */

// This feature test macro is required to expose clock_gettime().
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <unistd.h>

#include "egl.h"
#include "input.h"
#include "opengl.h"
#include "state.h"
#include "utils.h"
#include "wayland.h"

// Private Function Declarations

static void run_main_loop(struct glwall_state *state);

// Public Function Implementations

/**
 * @brief Main event loop
 *
 * Records the start time and enters the Wayland event dispatch loop.
 * Rendering is driven by frame callbacks, so this loop simply waits
 * for and dispatches Wayland events.
 *
 * @param state Pointer to global application state
 */
static void run_main_loop(struct glwall_state *state) {
    LOG_INFO("Starting render loop...");

    // The main loop simply dispatches Wayland events. Rendering is driven
    // by the frame callback requests. wl_display_dispatch() blocks until
    // an event is received.
    while (state->running && wl_display_dispatch(state->display) != -1) {
        // Intentionally empty.
    }
}

/**
 * @brief Main entry point
 *
 * Parses command-line options, initializes all subsystems, starts rendering,
 * runs the event loop, and cleans up on exit.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return EXIT_SUCCESS on normal exit
 */
int main(int argc, char *argv[]) {
    struct glwall_state state = {0};

    // Default configuration
    state.running = true;
    state.power_mode = GLWALL_POWER_MODE_FULL;
    state.mouse_overlay_mode = GLWALL_MOUSE_OVERLAY_NONE;
    state.mouse_overlay_edge_height = 32; // 32px edge strip by default when enabled
    state.audio_enabled = false;
    state.audio_source = GLWALL_AUDIO_SOURCE_PULSEAUDIO;
    state.audio_device_name = NULL;
    state.allow_vertex_shaders = false;
    state.vertex_shader_path = NULL;
    state.vertex_count = 262144;        // 512x512 points by default for vertex shaders
    state.vertex_draw_mode = GL_POINTS; // Default to points
    state.kernel_input_enabled = false;
    state.input_impl = NULL;

    parse_options(argc, argv, &state);

    if (!init_wayland(&state))
        goto cleanup;
    create_layer_surfaces(&state);
    if (!state.running)
        goto cleanup;

    if (!init_egl(&state))
        goto cleanup;
    if (!init_opengl(&state))
        goto cleanup;

    // Initialize kernel input if requested (optional, will log warnings on failure)
    if (state.kernel_input_enabled) {
        init_input(&state);
    }

    // Initialize start time BEFORE rendering begins
    clock_gettime(CLOCK_MONOTONIC, &state.start_time);

    start_rendering(&state);
    run_main_loop(&state);

cleanup:
    LOG_INFO("Cleaning up and exiting...");
    cleanup_input(&state);
    cleanup_opengl(&state);
    cleanup_egl(&state);
    cleanup_wayland(&state);
    return EXIT_SUCCESS;
}