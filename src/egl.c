/**
 * @file egl.c
 * @brief Implementation of EGL initialization and cleanup
 * 
 * This file handles EGL display setup, context creation with OpenGL 3.3
 * core profile, and window surface creation for each output.
 */

#include "egl.h"
#include "utils.h"

// Public Function Implementations

/**
 * @brief Initializes EGL display and creates contexts
 *
 * Gets the EGL display, initializes it, binds the OpenGL API, chooses
 * a configuration supporting 8-bit RGBA with OpenGL rendering, creates
 * an OpenGL 3.3 core profile context, and creates EGL window surfaces
 * for all outputs.
 *
 * @param state Pointer to global application state
 * @return true on success, false on failure
 *
 * @pre state->display must be initialized (Wayland connection)
 * @pre state->outputs must contain at least one output with a valid wl_egl_window
 * @post state->egl_display, state->egl_context, and output->egl_surface initialized
 */
bool init_egl(struct glwall_state *state) {
    state->egl_display = eglGetDisplay(state->display);
    if (state->egl_display == EGL_NO_DISPLAY) {
        LOG_ERROR("Failed to get EGL display.");
        return false;
    }

    if (!eglInitialize(state->egl_display, NULL, NULL)) {
        LOG_ERROR("Failed to initialize EGL.");
        return false;
    }

    // Bind the OpenGL API.
    if (!eglBindAPI(EGL_OPENGL_API)) {
        LOG_ERROR("Failed to bind OpenGL API.");
        return false;
    }

    // Request a desktop OpenGL context.
    EGLint const attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLint num_config;
    if (!eglChooseConfig(state->egl_display, attribs, &state->egl_config, 1, &num_config) || num_config < 1) {
        LOG_ERROR("Failed to choose EGL config.");
        return false;
    }

    // Request a 3.3 core profile context to match the shaders.
    EGLint const context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };

    state->egl_context = eglCreateContext(state->egl_display, state->egl_config, EGL_NO_CONTEXT, context_attribs);
    if (state->egl_context == EGL_NO_CONTEXT) {
        LOG_ERROR("Failed to create EGL context.");
        return false;
    }

    // Create EGL surfaces for each output.
    // The wl_egl_window for each output was already created in
    // create_layer_surfaces() to handle early configure events.
    // We just create the EGLSurface here.
    for (struct glwall_output *output = state->outputs; output; output = output->next) {
        // The wl_egl_window is now created in create_layer_surfaces().
        // Creating it again here would leak the original. We simply use
        // the one that already exists.
        if (!output->wl_egl_window) {
            LOG_ERROR("BUG: wl_egl_window was not created before EGL initialization.");
            return false;
        }
        output->egl_surface = eglCreateWindowSurface(state->egl_display, state->egl_config, (EGLNativeWindowType)output->wl_egl_window, NULL);
        if (output->egl_surface == EGL_NO_SURFACE) {
            LOG_ERROR("Failed to create EGL window surface for output %u. EGL Error: 0x%x", output->output_name, eglGetError());
            return false;
        }
    }

    LOG_INFO("EGL initialized successfully.");
    return true;
}

/**
 * @brief Cleans up EGL resources
 *
 * Unmakes the current EGL context, destroys all EGL window surfaces,
 * destroys the EGL context, and terminates the EGL display. This must
 * be called during application shutdown.
 *
 * @param state Pointer to global application state
 *
 * @note Safe to call even if init_egl() failed or was never called.
 */
void cleanup_egl(struct glwall_state *state) {
    if (state->egl_display == EGL_NO_DISPLAY) return;

    eglMakeCurrent(state->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    for (struct glwall_output *output = state->outputs; output; output = output->next) {
        if (output->egl_surface != EGL_NO_SURFACE) eglDestroySurface(state->egl_display, output->egl_surface);
        if (output->wl_egl_window) wl_egl_window_destroy(output->wl_egl_window);
    }
    if (state->egl_context != EGL_NO_CONTEXT) eglDestroyContext(state->egl_display, state->egl_context);
    eglTerminate(state->egl_display);
}