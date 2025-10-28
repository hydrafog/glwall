/**
 * @file state.h
 * @brief Global state structures for GLWall
 * 
 * This header defines the core data structures used throughout GLWall:
 * - glwall_state: Global application state
 * - glwall_output: Per-monitor/output state
 * 
 * CRITICAL: This header must be included first in all source files due to
 * strict OpenGL/EGL/Wayland header ordering requirements. GLEW must be
 * included before any other GL/EGL headers to function correctly.
 */

#ifndef STATE_H
#define STATE_H

#include <stdbool.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>

// Critical Include Order
//
// The following includes MUST appear in this exact order to avoid compilation
// errors and runtime issues:
//
// 1. GLEW must be included before other GL/EGL headers
#include <GL/glew.h>

// 2. EGL headers
#include <EGL/egl.h>
#include <EGL/eglext.h>

// 3. Core Wayland headers MUST come before protocol headers
#include <wayland-client.h>
#include <wayland-egl.h>

// Forward-declare the frame listener defined in wayland.c.
extern const struct wl_callback_listener frame_listener;

// 4. Generated protocol header (depends on wayland-client.h)
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

// Forward Declarations

struct glwall_state;

// Type Definitions

/**
 * @brief Represents a single monitor/output
 * 
 * Each connected display has its own glwall_output structure containing
 * Wayland surface, EGL surface, and rendering state.
 */
struct glwall_output {
    struct glwall_state *state;
    struct wl_output *wl_output;
    struct wl_surface *wl_surface;
    struct wl_egl_window *wl_egl_window;
    struct zwlr_layer_surface_v1 *layer_surface;
    EGLSurface egl_surface;

    uint32_t output_name;
    int32_t width;
    int32_t height;
    bool configured;
    struct wl_callback_listener frame_listener;
    struct glwall_output *next;
};

/**
 * @brief Represents the global application state
 * 
 * Contains all global state including Wayland connection, EGL context,
 * OpenGL shader program, and linked list of outputs.
 */
struct glwall_state {
    // Configuration
    const char *shader_path;
    bool debug;

    // Wayland State
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;

    // EGL State
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;

    // OpenGL State
    GLuint shader_program;
    GLuint vao;
    GLint loc_resolution;
    GLint loc_time;

    // Output Management
    struct glwall_output *outputs;

    // Runtime State
    bool running;
    struct timespec start_time;
};

#endif // STATE_H