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

#include "audio.h"
#include "input.h"
#include "opengl.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

// Constants
#define GLWALL_INFO_LOG_LEN 512

// Shader Source Code

static const char *vertex_shader_src = "#version 330 core\n"
                                       "const vec2 verts[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, "
                                       "-1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));\n"
                                       "void main() {\n"
                                       "    gl_Position = vec4(verts[gl_VertexID], 0.0, 1.0);\n"
                                       "}\n";

static const char *vertex_preamble = "#version 330 core\n"
                                     "#define vertexId float(gl_VertexID)\n"
                                     "uniform float vertexCount;\n"
                                     "uniform sampler2D sound;\n"
                                     "out vec4 v_color;\n";

static const char *fragment_preamble = "#version 330 core\n"
                                       "#define gl_FragColor fragColor\n"
                                       "out vec4 fragColor;\n"
                                       "in vec4 v_color;\n";

// Private Function Declarations

static GLuint compile_shader(struct glwall_state *state, GLenum type, const char *source);
static GLuint create_shader_program(struct glwall_state *state, const char *vert_src,
                                    const char *frag_src);
static char *concat_preamble(const char *preamble, const char *source);

// Private Function Implementations

/**
 * @brief Strips #version directive from shader source
 *
 * Removes any #version directive from the source code since GLWall adds its own.
 * Preserves all comments and other content, only removing the #version line.
 *
 * @param source Shader source code
 * @return New string with #version stripped, or NULL on allocation failure
 */
static char *strip_version_directive(const char *source) {
    // Search for #version directive anywhere in the source
    const char *line_start = source;
    const char *version_line = NULL;
    const char *version_end = NULL;
    
    while (*line_start) {
        // Skip whitespace at line start
        const char *content_start = line_start;
        while (*content_start && (*content_start == ' ' || *content_start == '\t')) {
            content_start++;
        }
        
        // Check if this line has #version
        if (strncmp(content_start, "#version", 8) == 0) {
            version_line = line_start;
            // Find end of this line
            version_end = strchr(content_start, '\n');
            if (version_end) {
                version_end++; // Include the newline
            } else {
                version_end = content_start + strlen(content_start);
            }
            break;
        }
        
        // Move to next line
        const char *next_line = strchr(line_start, '\n');
        if (next_line) {
            line_start = next_line + 1;
        } else {
            break;
        }
    }
    
    // If no #version found, return copy of original
    if (!version_line) {
        return strdup(source);
    }
    
    // Build result without the #version line
    size_t before_len = version_line - source;
    size_t after_len = strlen(version_end);
    char *result = malloc(before_len + after_len + 1);
    if (!result)
        return NULL;
    
    memcpy(result, source, before_len);
    memcpy(result + before_len, version_end, after_len);
    result[before_len + after_len] = '\0';
    
    return result;
}

/**
 * @brief Concatenates preamble and source into a new string
 */
static char *concat_preamble(const char *preamble, const char *source) {
    size_t plen = strlen(preamble);
    size_t slen = strlen(source);
    char *result = malloc(plen + slen + 1);
    if (!result)
        return NULL;
    strcpy(result, preamble);
    strcpy(result + plen, source);
    return result;
}

/**
 * @brief Compiles a shader from source
 *
 * Creates a shader object, compiles the source code, and checks for
 * compilation errors. On failure, logs the error and returns 0.
 *
 * @param state Pointer to global application state (for debug logging)
 * @param type Shader type (GL_VERTEX_SHADER or GL_FRAGMENT_SHADER)
 * @param source Shader source code string
 * @return Compiled shader ID, or 0 on failure
 *
 * @post On failure, the shader object is deleted and not returned
 */
static GLuint compile_shader(struct glwall_state *state, GLenum type, const char *source) {
    LOG_DEBUG(state, "Compiling shader of type %u", type);
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[GLWALL_INFO_LOG_LEN];
        glGetShaderInfoLog(shader, GLWALL_INFO_LOG_LEN, NULL, info_log);
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
 * Compiles vertex and fragment shaders and links them into a program.
 * Checks for compilation and linking errors. On any failure, logs the
 * error, cleans up allocated shaders, and returns 0.
 *
 * @param state Pointer to global application state (for debug logging)
 * @param vert_src Vertex shader source code
 * @param frag_src Fragment shader source code
 * @return Linked program ID, or 0 on failure
 *
 * @post Compiled shader objects are always deleted (whether linking succeeds or fails)
 */
static GLuint create_shader_program(struct glwall_state *state, const char *vert_src,
                                    const char *frag_src) {
    LOG_DEBUG(state, "Creating shader program...");
    GLuint vert = compile_shader(state, GL_VERTEX_SHADER, vert_src);
    GLuint frag = compile_shader(state, GL_FRAGMENT_SHADER, frag_src);
    if (!vert || !frag)
        return 0;

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
 * Initializes GLEW, creates a VAO, loads shader source from disk,
 * compiles and links the shader program, caches uniform locations,
 * and initializes the audio backend if enabled.
 *
 * @param state Pointer to global application state
 * @return true on success, false on failure
 *
 * @pre An EGL context must be current
 * @pre state->outputs must contain at least one output with egl_surface
 * @pre state->shader_path must point to a valid fragment shader file
 * @post state->shader_program, state->vao, and all uniform locations are initialized
 * @post Audio backend is initialized (if state->audio_enabled)
 */
bool init_opengl(struct glwall_state *state) {
    LOG_DEBUG(state, "Initializing OpenGL...");
    struct glwall_output *first_output = state->outputs;
    if (!first_output)
        return false;

    if (!eglMakeCurrent(state->egl_display, first_output->egl_surface, first_output->egl_surface,
                        state->egl_context)) {
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

    char *frag_src = NULL;
    if (state->shader_path) {
        char *raw_frag_src = read_file(state->shader_path);
        if (!raw_frag_src)
            return false;
        // Strip #version directive if present (GLWall adds its own)
        char *stripped_frag_src = strip_version_directive(raw_frag_src);
        free(raw_frag_src);
        if (!stripped_frag_src)
            return false;
        // Inject preamble into fragment shader
        frag_src = concat_preamble(fragment_preamble, stripped_frag_src);
        free(stripped_frag_src);
    } else {
        // Default passthrough fragment shader
        const char *passthrough_src = "void main() {\n"
                                      "    fragColor = v_color;\n"
                                      "}\n";
        frag_src = concat_preamble(fragment_preamble, passthrough_src);
    }

    if (!frag_src)
        return false;

    char *vert_src = NULL;
    if (state->allow_vertex_shaders && state->vertex_shader_path) {
        char *raw_vert_src = read_file(state->vertex_shader_path);
        if (!raw_vert_src) {
            LOG_ERROR("Failed to read vertex shader '%s'", state->vertex_shader_path);
            free(frag_src);
            return false;
        }
        // Strip #version directive if present (GLWall adds its own)
        char *stripped_vert_src = strip_version_directive(raw_vert_src);
        free(raw_vert_src);
        if (!stripped_vert_src) {
            free(frag_src);
            return false;
        }
        // Inject preamble into vertex shader
        vert_src = concat_preamble(vertex_preamble, stripped_vert_src);
        free(stripped_vert_src);
        if (!vert_src) {
            free(frag_src);
            return false;
        }
    }

    const char *vs = vert_src ? vert_src : vertex_shader_src;
    state->shader_program = create_shader_program(state, vs, frag_src);
    free(frag_src);
    if (vert_src)
        free(vert_src);

    if (!state->shader_program)
        return false;

    // Cache uniform locations. Missing uniforms will return -1.
    // Support both Shadertoy names (iTime) and generic names (time).
    state->loc_resolution = glGetUniformLocation(state->shader_program, "iResolution");
    state->loc_resolution_vec2 = glGetUniformLocation(state->shader_program, "resolution");

    state->loc_time = glGetUniformLocation(state->shader_program, "iTime");
    if (state->loc_time == -1)
        state->loc_time = glGetUniformLocation(state->shader_program, "time");

    state->loc_time_delta = glGetUniformLocation(state->shader_program, "iTimeDelta");
    state->loc_frame = glGetUniformLocation(state->shader_program, "iFrame");

    state->loc_mouse = glGetUniformLocation(state->shader_program, "iMouse");
    state->loc_mouse_vec2 = glGetUniformLocation(state->shader_program, "mouse");

    state->loc_sound = glGetUniformLocation(
        state->shader_program, "sound"); // Shadertoy uses iChannel0 usually, but user used 'sound'
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
 * Deletes the shader program, VAO, and audio resources. This must be
 * called during application shutdown before the EGL context is destroyed.
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
 * updates all shader uniforms (iTime, iResolution, iMouse, audio, etc.),
 * renders the scene, swaps buffers, and requests the next frame callback
 * to continue the animation loop.
 *
 * @param output Pointer to the output to render
 *
 * @pre output->configured must be true
 * @pre OpenGL must be initialized
 * @pre The output's EGL surface must be valid
 */
void render_frame(struct glwall_output *output) {
    struct glwall_state *state = output->state;
    if (!output->configured) {
        LOG_DEBUG(state, "Skipping unconfigured output %u", output->output_name);
        return;
    }

    LOG_DEBUG(state, "Rendering for output %u (%ux%u)", output->output_name, output->width,
              output->height);

    // Poll kernel input events if enabled (updates pointer_x/y)
    if (state->kernel_input_enabled) {
        poll_input_events(state);
    }

    eglMakeCurrent(state->egl_display, output->egl_surface, output->egl_surface,
                   state->egl_context);

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
        min_dt = 0.0f; // Always update
        break;
    case GLWALL_POWER_MODE_THROTTLED:
        min_dt = 1.0f / 30.0f; // ~30 Hz logical updates
        break;
    case GLWALL_POWER_MODE_PAUSED:
        min_dt = 1.0f; // ~1 Hz logical updates
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

    // iResolution (vec3)
    if (state->loc_resolution != -1) {
        glUniform3f(state->loc_resolution, (float)output->width, (float)output->height, 1.0f);
    }
    // resolution (vec2)
    if (state->loc_resolution_vec2 != -1) {
        glUniform2f(state->loc_resolution_vec2, (float)output->width, (float)output->height);
    }

    // iMouse / mouse
    if (state->loc_mouse != -1 || state->loc_mouse_vec2 != -1) {
        float mx = 0.0f, my = 0.0f, mz = 0.0f, mw = 0.0f;
        if (state->pointer_output == output) {
            mx = (float)state->pointer_x;
            my = (float)(output->height - 1) - (float)state->pointer_y; // flip Y to match GL coords
            if (state->pointer_down) {
                mz = (float)state->pointer_down_x;
                mw = (float)(output->height - 1) - (float)state->pointer_down_y;
            }
        }
        if (state->loc_mouse != -1) {
            glUniform4f(state->loc_mouse, mx, my, mz, mw);
        }
        if (state->loc_mouse_vec2 != -1) {
            glUniform2f(state->loc_mouse_vec2, mx, my);
        }
    }

    // Audio uniforms
    if (state->audio_enabled && state->audio.backend_ready) {
        if (state->loc_sound != -1 && state->audio.texture != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, state->audio.texture);
            glUniform1i(state->loc_sound, 0);
        }
        if (state->loc_sound_res != -1) {
            glUniform2f(state->loc_sound_res, (float)state->audio.tex_width,
                        (float)state->audio.tex_height);
        }
    }

    // vertexCount uniform for custom vertex shaders
    if (state->loc_vertex_count != -1 && state->allow_vertex_shaders) {
        glUniform1f(state->loc_vertex_count, (float)state->vertex_count);
    }

    LOG_DEBUG(state, "Uniforms: iTime=%.2f, iTimeDelta=%.4f, iFrame=%d, iResolution=%dx%dx1.0",
              shader_time, time_delta, current_frame, output->width, output->height);

    // Enable Blending and Depth Test for visualizers
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Draw geometry: fullscreen quad by default, or custom primitives for vertex shaders.
    if (state->allow_vertex_shaders && state->vertex_shader_path) {
        glDrawArrays(state->vertex_draw_mode, 0, state->vertex_count);
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