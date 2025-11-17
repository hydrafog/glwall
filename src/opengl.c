/**
 * @file opengl.c
 * @brief Implementation of OpenGL shader compilation and rendering
 * 
 * This file handles shader compilation, program linking, and per-frame
 * rendering with shader uniform updates.
 */

// This feature test macro is required to expose clock_gettime().
#define _POSIX_C_SOURCE 200809L
#include <time.h>

#include <stdlib.h>
#include "opengl.h"
#include "utils.h"
#include "audio.h"

// Shader Source Code

static const char *vertex_shader_src =
    "#version 330 core\n"
    "const vec2 verts[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));\n"
    "void main() {\n"
    "    gl_Position = vec4(verts[gl_VertexID], 0.0, 1.0);\n"
    "}\n";

// Private Function Declarations

static GLuint compile_shader(struct glwall_state *state, GLenum type, const char *source);
static GLuint create_shader_program(struct glwall_state *state, const char *vert_src, const char *frag_src);

// Private Function Implementations

/**
 * @brief Compiles a shader from source
 * 
 * Creates a shader object, compiles the source, and checks for errors.
 * 
 * @param state Pointer to global application state (for debug logging)
 * @param type Shader type (GL_VERTEX_SHADER or GL_FRAGMENT_SHADER)
 * @param source Shader source code string
 * @return Compiled shader ID, or 0 on failure
 */
static GLuint compile_shader(struct glwall_state *state, GLenum type, const char *source) {
    LOG_DEBUG(state, "Compiling shader of type %u", type);
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, NULL, info_log);
        LOG_ERROR("Shader compilation failed:\n%s", info_log);
        glDeleteShader(shader);
        return 0;
    }
    LOG_DEBUG(state, "Shader compiled successfully.");
    return shader;
}

/**
 * @brief Creates and links a shader program
 * 
 * Compiles vertex and fragment shaders, links them into a program,
 * and checks for linking errors.
 * 
 * @param state Pointer to global application state (for debug logging)
 * @param vert_src Vertex shader source code
 * @param frag_src Fragment shader source code
 * @return Linked program ID, or 0 on failure
 */
static GLuint create_shader_program(struct glwall_state *state, const char *vert_src, const char *frag_src) {
    LOG_DEBUG(state, "Creating shader program...");
    GLuint vert = compile_shader(state, GL_VERTEX_SHADER, vert_src);
    GLuint frag = compile_shader(state, GL_FRAGMENT_SHADER, frag_src);
    if (!vert || !frag) return 0;

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint log_len;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
        char *log = malloc(log_len);
        glGetProgramInfoLog(program, log_len, NULL, log);
        LOG_ERROR("Shader program linking failed:\n%s", log);
        free(log);
        glDeleteProgram(program);
        return 0;
    }
    LOG_DEBUG(state, "Shader program created successfully.");
    return program;
}

// Public Function Implementations

/**
 * @brief Initializes OpenGL context and compiles shaders
 * 
 * Initializes GLEW, creates a VAO, loads the fragment shader from disk,
 * compiles and links the shader program, and retrieves uniform locations.
 * 
 * @param state Pointer to global application state
 * @return true on success, false on failure
 */
bool init_opengl(struct glwall_state *state) {
    LOG_DEBUG(state, "Initializing OpenGL...");
    struct glwall_output *first_output = state->outputs;
    if (!first_output) return false;

    if (!eglMakeCurrent(state->egl_display, first_output->egl_surface, first_output->egl_surface, state->egl_context)) {
        LOG_ERROR("Failed to make EGL context current.");
        return false;
    }
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        LOG_ERROR("GLEW initialization failed.");
        return false;
    }
    LOG_INFO("Using GLEW %s", glewGetString(GLEW_VERSION));

    // A VAO is required for core profile OpenGL.
    glGenVertexArrays(1, &state->vao);
    glBindVertexArray(state->vao);

    char *frag_src = read_file(state->shader_path);
    if (!frag_src) return false;

    char *vert_src = NULL;
    if (state->allow_vertex_shaders && state->vertex_shader_path) {
        vert_src = read_file(state->vertex_shader_path);
        if (!vert_src) {
            LOG_ERROR("Failed to read vertex shader '%s'", state->vertex_shader_path);
            free(frag_src);
            return false;
        }
    }

    const char *vs = vert_src ? vert_src : vertex_shader_src;
    state->shader_program = create_shader_program(state, vs, frag_src);
    free(frag_src);
    if (vert_src) free(vert_src);

    if (!state->shader_program) return false;

    // Cache uniform locations. Missing uniforms will return -1, which is safe
    // to pass to glUniform* (no-op), so shaders can omit unused ones.
    state->loc_resolution = glGetUniformLocation(state->shader_program, "iResolution");
    state->loc_time = glGetUniformLocation(state->shader_program, "iTime");
    state->loc_time_delta = glGetUniformLocation(state->shader_program, "iTimeDelta");
    state->loc_frame = glGetUniformLocation(state->shader_program, "iFrame");
    state->loc_mouse = glGetUniformLocation(state->shader_program, "iMouse");
    state->loc_sound = glGetUniformLocation(state->shader_program, "sound");
    state->loc_sound_res = glGetUniformLocation(state->shader_program, "soundRes");
    state->loc_vertex_count = glGetUniformLocation(state->shader_program, "vertexCount");

    // Initialize audio backend if enabled. This assumes an active GL context.
    if (!init_audio(state)) {
        LOG_ERROR("Audio initialization failed, disabling audio.");
        state->audio_enabled = false;
    }

    LOG_DEBUG(state, "OpenGL initialized successfully.");
    return true;
}

/**
 * @brief Cleans up OpenGL resources
 * 
 * Deletes the shader program, VAO, and audio resources.
 * 
 * @param state Pointer to global application state
 */
void cleanup_opengl(struct glwall_state *state) {
    LOG_DEBUG(state, "Cleaning up OpenGL...");

    // Clean up audio texture and backend before destroying GL context.
    cleanup_audio(state);

    if (state->shader_program) {
        glDeleteProgram(state->shader_program);
    }
    if (state->vao) {
        glDeleteVertexArrays(1, &state->vao);
    }
}

/**
 * @brief Renders a single frame for the given output
 * 
 * Makes the output's EGL surface current, calculates elapsed time,
 * updates shader uniforms, renders a fullscreen quad, swaps buffers,
 * and requests the next frame callback.
 * 
 * @param output Pointer to the output to render
 */
void render_frame(struct glwall_output *output) {
    struct glwall_state *state = output->state;
    if (!output->configured) {
        LOG_DEBUG(state, "Skipping unconfigured output %u", output->output_name);
        return;
    }

    LOG_DEBUG(state, "Rendering for output %u (%ux%u)", output->output_name, output->width, output->height);
    eglMakeCurrent(state->egl_display, output->egl_surface, output->egl_surface, state->egl_context);
    
    // The VAO must be bound every time the context is made current.
    glBindVertexArray(state->vao);

    glViewport(0, 0, output->width, output->height);

    // Calculate elapsed time for the shader.
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    float time_sec = (current_time.tv_sec - state->start_time.tv_sec) +
                     (current_time.tv_nsec - state->start_time.tv_nsec) / 1e9f;

    // Decide whether to advance shader time based on power mode.
    float dt_real;
    if (state->frame_index == 0) {
        dt_real = 0.0f;
        state->last_time_sec = time_sec;
    } else {
        dt_real = time_sec - state->last_time_sec;
    }

    float min_dt = 0.0f;
    switch (state->power_mode) {
        case GLWALL_POWER_MODE_FULL:
            min_dt = 0.0f;          // Always update
            break;
        case GLWALL_POWER_MODE_THROTTLED:
            min_dt = 1.0f / 30.0f;  // ~30 Hz logical updates
            break;
        case GLWALL_POWER_MODE_PAUSED:
            min_dt = 1.0f;          // ~1 Hz logical updates
            break;
        default:
            min_dt = 0.0f;
            break;
    }

    bool do_update = (state->frame_index == 0) || (dt_real >= min_dt);
    float time_delta = 0.0f;
    if (do_update) {
        state->logical_time_sec += dt_real;
        time_delta = dt_real;
        state->last_time_sec = time_sec;
        state->frame_index++;
    }

    float shader_time = state->logical_time_sec;
    int current_frame = state->frame_index;

    // Update audio texture (if enabled) before drawing. We only need to
    // update when advancing logical time; reusing the previous texture
    // reduces CPU usage when throttled.
    if (do_update) {
        update_audio_texture(state);
    }

    glUseProgram(state->shader_program);

    // Update shader uniforms.
    // iTime / iTimeDelta / iFrame
    if (state->loc_time != -1) {
        glUniform1f(state->loc_time, shader_time);
    }
    if (state->loc_time_delta != -1) {
        glUniform1f(state->loc_time_delta, time_delta);
    }
    if (state->loc_frame != -1) {
        glUniform1i(state->loc_frame, current_frame);
    }

    // iResolution
    if (state->loc_resolution != -1) {
        glUniform3f(state->loc_resolution, (float)output->width, (float)output->height, 1.0f);
    }

    // iMouse: (x, y, clickX, clickY) in pixel coordinates, or zeros if pointer is not over this output.
    if (state->loc_mouse != -1) {
        float mx = 0.0f, my = 0.0f, mz = 0.0f, mw = 0.0f;
        if (state->pointer_output == output) {
            mx = (float)state->pointer_x;
            my = (float)(output->height - 1) - (float)state->pointer_y; // flip Y to match GL coords
            if (state->pointer_down) {
                mz = (float)state->pointer_down_x;
                mw = (float)(output->height - 1) - (float)state->pointer_down_y;
            }
        }
        glUniform4f(state->loc_mouse, mx, my, mz, mw);
    }

    // Audio uniforms
    if (state->audio_enabled && state->audio.backend_ready) {
        if (state->loc_sound != -1 && state->audio.texture != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, state->audio.texture);
            glUniform1i(state->loc_sound, 0);
        }
        if (state->loc_sound_res != -1) {
            glUniform2f(state->loc_sound_res,
                        (float)state->audio.tex_width,
                        (float)state->audio.tex_height);
        }
    }

    // vertexCount uniform for custom vertex shaders
    if (state->loc_vertex_count != -1 && state->allow_vertex_shaders) {
        glUniform1f(state->loc_vertex_count, (float)state->vertex_count);
    }

    LOG_DEBUG(state, "Uniforms: iTime=%.2f, iTimeDelta=%.4f, iFrame=%d, iResolution=%dx%dx1.0", shader_time, time_delta, current_frame, output->width, output->height);

    glClear(GL_COLOR_BUFFER_BIT);

    // Draw geometry: fullscreen quad by default, or point cloud for custom vertex shaders.
    if (state->allow_vertex_shaders && state->vertex_shader_path) {
        glDrawArrays(GL_POINTS, 0, state->vertex_count);
    } else {
        // The built-in vertex shader uses gl_VertexID to generate a fullscreen quad.
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    GLenum err;
    if ((err = glGetError()) != GL_NO_ERROR) {
        LOG_ERROR("OpenGL error during render: 0x%x", err);
    }

    eglSwapBuffers(state->egl_display, output->egl_surface);
    LOG_DEBUG(state, "Swapped buffers for output %u", output->output_name);

    // Request the next frame callback to continue the animation loop.
    struct wl_callback *cb = wl_surface_frame(output->wl_surface);
    wl_callback_add_listener(cb, &frame_listener, output);
    wl_surface_commit(output->wl_surface);
}