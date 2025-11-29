#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <unistd.h>

#include "egl.h"
#include "input.h"
#include "opengl.h"
#include "state.h"
#include "utils.h"
#include "wayland.h"

static void run_main_loop(struct glwall_state *state);

static void run_main_loop(struct glwall_state *state) {
    LOG_INFO("%s", "Render loop started");

    while (state->running && wl_display_dispatch(state->display) != -1) {
    }
}

int main(int argc, char *argv[]) {
    struct glwall_state state = {0};
    LOG_DEBUG(&state, "Application initialization started (argc: %d)", argc);

    state.running = true;
    state.power_mode = GLWALL_POWER_MODE_FULL;
    state.mouse_overlay_mode = GLWALL_MOUSE_OVERLAY_NONE;
    state.mouse_overlay_edge_height_px = 32;
    state.audio_enabled = false;
    state.audio_source = GLWALL_AUDIO_SOURCE_PULSEAUDIO;
    state.audio_device_name = NULL;
    state.allow_vertex_shaders = false;
    state.vertex_shader_path = NULL;
    state.vertex_count = 262144;
    state.vertex_draw_mode = GL_POINTS;
    state.kernel_input_enabled = false;
    state.input_impl = NULL;

    parse_options(argc, argv, &state);
    LOG_DEBUG(&state, "%s", "Configuration parsing completed");

    if (!init_wayland(&state))
        goto cleanup;
    LOG_DEBUG(&state, "%s", "Wayland subsystem initialization succeeded");
    create_layer_surfaces(&state);
    LOG_DEBUG(&state, "%s", "Layer surfaces created");
    if (!state.running)
        goto cleanup;

    if (!init_egl(&state))
        goto cleanup;
    LOG_DEBUG(&state, "%s", "EGL subsystem initialization succeeded");
    if (!init_opengl(&state))
        goto cleanup;
    LOG_DEBUG(&state, "%s", "OpenGL subsystem initialization succeeded");

    if (state.kernel_input_enabled) {
        init_input(&state);
        LOG_DEBUG(&state, "%s", "Input subsystem initialization completed");
    }

    clock_gettime(CLOCK_MONOTONIC, &state.start_time);
    LOG_DEBUG(&state, "%s", "Frame timer initialized");

    start_rendering(&state);
    run_main_loop(&state);

cleanup:
    LOG_INFO("%s", "Application shutdown initiated");
    LOG_DEBUG(&state, "%s", "Cleanup sequence: terminating input subsystem");
    cleanup_input(&state);
    LOG_DEBUG(&state, "%s", "Cleanup sequence: terminating OpenGL subsystem");
    cleanup_opengl(&state);
    LOG_DEBUG(&state, "%s", "Cleanup sequence: terminating EGL subsystem");
    cleanup_egl(&state);
    LOG_DEBUG(&state, "%s", "Cleanup sequence: terminating Wayland subsystem");
    cleanup_wayland(&state);
    LOG_DEBUG(&state, "%s", "Application shutdown completed");
    return EXIT_SUCCESS;
}