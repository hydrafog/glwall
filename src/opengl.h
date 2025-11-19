/**
 * @file opengl.h
 * @brief OpenGL shader initialization and rendering for GLWall
 * 
 * This module handles OpenGL context setup, shader compilation and linking,
 * and per-frame rendering for each output.
 */

#pragma once

#include "state.h"

/**
 * @brief Initializes OpenGL context and compiles shaders
 * 
 * Initializes GLEW, creates a VAO, loads and compiles the fragment shader,
 * and retrieves uniform locations for iResolution and iTime.
 * 
 * @param state Pointer to global application state
 * @return true on success, false on failure
 * 
 * @pre EGL context must be current
 * @pre state->shader_path must point to a valid GLSL fragment shader
 * @post state->shader_program and state->vao initialized on success
 */
bool init_opengl(struct glwall_state *state);

/**
 * @brief Cleans up OpenGL resources
 * 
 * Deletes the shader program and VAO.
 * 
 * @param state Pointer to global application state
 */
void cleanup_opengl(struct glwall_state *state);

/**
 * @brief Renders a single frame for the given output
 * 
 * Makes the output's EGL surface current, updates shader uniforms,
 * renders a fullscreen quad, swaps buffers, and requests the next frame.
 * 
 * @param output Pointer to the output to render
 * 
 * @pre output->configured must be true
 * @pre OpenGL context must be initialized
 */
void render_frame(struct glwall_output *output);

#endif // OPENGL_H