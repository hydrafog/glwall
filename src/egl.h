/**
 * @file egl.h
 * @brief EGL initialization and cleanup for GLWall
 *
 * This module handles EGL display initialization, context creation,
 * and window surface setup for each output.
 */

#pragma once

#include "state.h"

/**
 * @brief Initializes EGL display and creates contexts
 *
 * Sets up the EGL display, binds the OpenGL API, chooses an appropriate
 * EGL configuration, creates an OpenGL 3.3 core profile context, and
 * creates EGL window surfaces for each output.
 *
 * @param state Pointer to global application state
 * @return true on success, false on failure
 *
 * @pre state->display must be initialized
 * @pre state->outputs must contain at least one output
 * @post state->egl_display, state->egl_context initialized on success
 */
bool init_egl(struct glwall_state *state);

/**
 * @brief Cleans up EGL resources
 *
 * Destroys all EGL surfaces, contexts, and terminates the EGL display.
 *
 * @param state Pointer to global application state
 */
void cleanup_egl(struct glwall_state *state);
