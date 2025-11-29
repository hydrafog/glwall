#include "egl.h"
#include "utils.h"

bool init_egl(struct glwall_state *state) {
    state->egl_display = eglGetDisplay(state->display);
    if (state->egl_display == EGL_NO_DISPLAY) {
        LOG_ERROR("%s", "EGL subsystem error: unable to obtain EGL display");
        return false;
    }

    if (!eglInitialize(state->egl_display, NULL, NULL)) {
        LOG_ERROR("%s", "EGL subsystem error: initialization failed");
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_API)) {
        LOG_ERROR("%s", "EGL subsystem error: unable to bind OpenGL API");
        return false;
    }

    EGLint const attribs[] = {EGL_SURFACE_TYPE,
                              EGL_WINDOW_BIT,
                              EGL_RENDERABLE_TYPE,
                              EGL_OPENGL_BIT,
                              EGL_RED_SIZE,
                              8,
                              EGL_GREEN_SIZE,
                              8,
                              EGL_BLUE_SIZE,
                              8,
                              EGL_ALPHA_SIZE,
                              8,
                              EGL_NONE};

    EGLint num_config;
    if (!eglChooseConfig(state->egl_display, attribs, &state->egl_config, 1, &num_config) ||
        num_config < 1) {
        LOG_ERROR("%s", "EGL subsystem error: unable to select EGL configuration");
        return false;
    }
    LOG_DEBUG(state, "EGL subsystem: configuration selected (index: 0 from %d candidates)",
              num_config);

    EGLint const context_attribs[] = {EGL_CONTEXT_MAJOR_VERSION,
                                      3,
                                      EGL_CONTEXT_MINOR_VERSION,
                                      3,
                                      EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                      EGL_NONE};

    state->egl_context =
        eglCreateContext(state->egl_display, state->egl_config, EGL_NO_CONTEXT, context_attribs);
    if (state->egl_context == EGL_NO_CONTEXT) {
        LOG_ERROR("%s", "Failed to create EGL context.");
        return false;
    }
    LOG_DEBUG(state, "%s", "EGL subsystem: context created with OpenGL 3.3 Core Profile");

    for (struct glwall_output *output = state->outputs; output; output = output->next) {

        if (!output->wl_egl_window) {
            LOG_ERROR("%s", "EGL subsystem error: internal error - EGL window not initialized "
                            "before context creation");
            return false;
        }
        output->egl_surface =
            eglCreateWindowSurface(state->egl_display, state->egl_config,
                                   (EGLNativeWindowType)output->wl_egl_window, NULL);
        if (output->egl_surface == EGL_NO_SURFACE) {
            LOG_ERROR("EGL subsystem error: unable to create window surface for output %u (EGL "
                      "error: 0x%x)",
                      output->output_name, eglGetError());
            return false;
        }
    }

    LOG_INFO("%s", "EGL subsystem initialization completed successfully");
    return true;
}

void cleanup_egl(struct glwall_state *state) {
    if (state->egl_display == EGL_NO_DISPLAY)
        return;

    eglMakeCurrent(state->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    for (struct glwall_output *output = state->outputs; output; output = output->next) {
        if (output->egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(state->egl_display, output->egl_surface);
        if (output->wl_egl_window)
            wl_egl_window_destroy(output->wl_egl_window);
    }
    if (state->egl_context != EGL_NO_CONTEXT)
        eglDestroyContext(state->egl_display, state->egl_context);
    eglTerminate(state->egl_display);
}