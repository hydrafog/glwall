#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include <GL/glew.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <wayland-client.h>
#include <wayland-egl.h>

extern const struct wl_callback_listener frame_listener;

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct glwall_state;

struct glwall_preset;
struct glwall_pipeline;

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
    GLWALL_AUDIO_SOURCE_FAKE,
};

struct glwall_audio_state {
    bool enabled;
    bool backend_ready;
    GLuint texture;
    int32_t tex_width_px;
    int32_t tex_height_px;
    void *impl;
};

struct glwall_output {
    struct glwall_state *state;
    struct wl_output *wl_output;
    struct wl_surface *wl_surface;
    struct wl_egl_window *wl_egl_window;
    struct zwlr_layer_surface_v1 *layer_surface;
    EGLSurface egl_surface;

    struct wl_surface *overlay_surface;
    struct zwlr_layer_surface_v1 *overlay_layer_surface;

    uint32_t output_name;
    int32_t width_px;
    int32_t height_px;
    bool configured;
    int32_t last_resolution_w;
    int32_t last_resolution_h;
    int loc_resolution_last_updated;
    struct wl_callback_listener frame_listener;
    struct glwall_output *next;
};

struct glwall_state {

    const char *shader_path;
    const char *image_path;
    bool debug;

    enum glwall_power_mode power_mode;
    enum glwall_mouse_overlay_mode mouse_overlay_mode;
    int32_t mouse_overlay_edge_height_px;
    bool audio_enabled;
    enum glwall_audio_source audio_source;
    const char *audio_device_name;
    bool allow_vertex_shaders;
    const char *vertex_shader_path;
    int32_t vertex_count;
    GLenum vertex_draw_mode;
    bool kernel_input_enabled;
    uint32_t layer;

    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_seat *seat;
    struct wl_pointer *pointer;

    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;

    GLuint shader_program;
    GLuint vao;
    GLuint ubo_state;
    GLuint current_program;

    GLuint source_image_texture;
    int32_t source_image_width_px;
    int32_t source_image_height_px;
    GLint loc_resolution;
    GLint loc_resolution_vec2;
    GLint loc_time;
    GLint loc_time_delta;
    GLint loc_frame;
    GLint loc_mouse;
    GLint loc_mouse_vec2;
    GLint loc_sound;
    GLint loc_sound_res;
    GLint loc_vertex_count;

    struct glwall_pipeline *pipeline;

    struct glwall_output *outputs;

    struct glwall_output *pointer_output;
    double pointer_x;
    double pointer_y;
    double pointer_down_x;
    double pointer_down_y;
    bool pointer_down;

    struct glwall_audio_state audio;

    void *input_impl;

    bool running;
    struct timespec start_time;
    float last_time_sec;
    float logical_time_sec;
    int frame_index;
    bool profiling_enabled;
    double profiling_last_frame_ms;
};
