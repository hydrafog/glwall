/**
 * @file wayland.h
 * @brief Wayland compositor integration for GLWall
 * 
 * This module handles Wayland display connection, output discovery,
 * layer surface creation, and event handling.
 */

#ifndef WAYLAND_H
#define WAYLAND_H

#include "state.h"

/**
 * @brief Initializes Wayland connection and discovers outputs
 * 
 * Connects to the Wayland display, retrieves the registry, binds to
 * compositor and layer-shell interfaces, and discovers all outputs.
 * 
 * @param state Pointer to global application state
 * @return true on success, false on failure
 * 
 * @post state->display, state->compositor, state->layer_shell initialized
 * @post state->outputs contains linked list of discovered outputs
 */
bool init_wayland(struct glwall_state *state);

/**
 * @brief Cleans up Wayland resources
 * 
 * Destroys all layer surfaces, Wayland surfaces, outputs, and disconnects
 * from the display.
 * 
 * @param state Pointer to global application state
 */
void cleanup_wayland(struct glwall_state *state);

/**
 * @brief Creates layer surfaces for all outputs
 * 
 * Creates a Wayland surface and wlr-layer-shell layer surface for each
 * discovered output, configures them as fullscreen background layers,
 * and creates EGL windows.
 * 
 * @param state Pointer to global application state
 * 
 * @pre init_wayland() must have been called successfully
 */
void create_layer_surfaces(struct glwall_state *state);

/**
 * @brief Starts the rendering loop for all configured outputs
 * 
 * Triggers the initial render_frame() call for each configured output,
 * which begins the frame callback loop.
 * 
 * @param state Pointer to global application state
 * 
 * @pre OpenGL must be initialized
 * @pre Outputs must be configured
 */
void start_rendering(struct glwall_state *state);

#endif // WAYLAND_H