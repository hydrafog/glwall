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

// Configuration Enums

enum glwall_power_mode {
    GLWALL_POWER_MODE_FULL,
    GLWALL_POWER_MODE_THROTTLED,
    GLWALL_POWER_MODE_PAUSED,
};

enum glwall_mouse_overlay_mode {
    GLWALL_MOUSE_OVERLAY_NONE,
    GLWALL_MOUSE_OVERLAY_EDGE,
    GLWALL_MOUSE_OVERLAY_FULL,
};

enum glwall_audio_source {
    GLWALL_AUDIO_SOURCE_NONE,
    GLWALL_AUDIO_SOURCE_PULSEAUDIO,
};

// Audio state stored in glwall_state. Backend-specific details are hidden
// behind the opaque impl pointer managed by audio.c.
struct glwall_audio_state {
    bool enabled;           // Audio reactive shaders enabled at runtime
    bool backend_ready;     // Backend (e.g. PulseAudio) initialized
    GLuint texture;         // OpenGL texture containing audio data
    int tex_width;          // Width of audio texture in pixels
    int tex_height;         // Height of audio texture in pixels
    void *impl;             // Opaque backend implementation (owned by audio.c)
};

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
    struct wl_surface *wl_surface;             // Main wallpaper surface
    struct wl_egl_window *wl_egl_window;
    struct zwlr_layer_surface_v1 *layer_surface; // Main background layer surface
    EGLSurface egl_surface;

    // Optional input-only overlay surface used for mouse overlay modes.
    struct wl_surface *overlay_surface;
    struct zwlr_layer_surface_v1 *overlay_layer_surface;

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
 * OpenGL shader program, input state, audio, and linked list of outputs.
 */
struct glwall_state {
    // Configuration
    const char *shader_path;
    bool debug;

    enum glwall_power_mode power_mode;          // Power policy for rendering
    enum glwall_mouse_overlay_mode mouse_overlay_mode; // Experimental mouse overlay
    int mouse_overlay_edge_height;              // Height (in px) of edge input strip when using EDGE mode
    bool audio_enabled;                         // Config: enable audio reactive shaders
    enum glwall_audio_source audio_source;      // Selected audio backend
    bool allow_vertex_shaders;                  // Allow custom vertex shaders
    const char *vertex_shader_path;             // Optional path to vertex shader
    int vertex_count;                           // Number of vertices when using custom vertex shader

    // Wayland State
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_seat *seat;           // Pointer/keyboard seat (optional)
    struct wl_pointer *pointer;     // Pointer device (optional)

    // EGL State
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;

    // OpenGL State
    GLuint shader_program;
    GLuint vao;
    GLint loc_resolution;
    GLint loc_time;
    GLint loc_time_delta;           // iTimeDelta uniform location
    GLint loc_frame;                // iFrame uniform location
    GLint loc_mouse;                // iMouse uniform location
    GLint loc_sound;                // sampler2D sound uniform location
    GLint loc_sound_res;            // vec2 soundRes uniform location
    GLint loc_vertex_count;         // vertexCount uniform location

    // Output Management
    struct glwall_output *outputs;

    // Input State (for iMouse)
    struct glwall_output *pointer_output; // Output currently under the pointer (if any)
    double pointer_x;                      // Pointer x in surface coordinates (pixels)
    double pointer_y;                      // Pointer y in surface coordinates (pixels)
    double pointer_down_x;                 // Pointer down position (pixels)
    double pointer_down_y;                 // Pointer down position (pixels)
    bool pointer_down;                     // Whether the primary button is pressed

    // Audio State
    struct glwall_audio_state audio;

    // Runtime State
    bool running;
    struct timespec start_time;
    float last_time_sec;                   // Last real time when uniforms were updated
    float logical_time_sec;                // Accumulated shader time for iTime
    int frame_index;                       // Frame counter (for iFrame)
};

#endif // STATE_H