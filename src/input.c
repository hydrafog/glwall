/**
 * @file input.c
 * @brief Kernel-level input device monitoring using libevdev
 * 
 * Directly reads mouse/pointer events from /dev/input/event* devices,
 * bypassing Wayland compositor input restrictions. This allows the wallpaper
 * to track mouse position even when behind other windows.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <linux/input.h>
#include <libevdev/libevdev.h>

#include "input.h"
#include "utils.h"

#define MAX_INPUT_DEVICES 16
#define INPUT_DEVICE_PATH "/dev/input"

// Private input state stored in glwall_state
struct input_device {
    int fd;
    struct libevdev *dev;
    bool is_absolute;  // true for touchpads/tablets, false for mice
};

struct input_state {
    struct input_device devices[MAX_INPUT_DEVICES];
    int device_count;
    int screen_width;   // For normalizing absolute coordinates
    int screen_height;
};

/**
 * @brief Check if device has pointer capabilities
 *
 * Tests whether the given libevdev device supports relative or absolute
 * pointer input (mouse, touchpad, touchscreen, etc.).
 *
 * @param dev libevdev device to check
 * @return true if device supports pointer input, false otherwise
 */
static bool is_pointer_device(struct libevdev *dev) {
    // Check for relative pointer (mouse)
    if (libevdev_has_event_code(dev, EV_REL, REL_X) &&
        libevdev_has_event_code(dev, EV_REL, REL_Y)) {
        return true;
    }

    // Check for absolute pointer (touchpad/tablet/touchscreen)
    if (libevdev_has_event_code(dev, EV_ABS, ABS_X) &&
        libevdev_has_event_code(dev, EV_ABS, ABS_Y)) {
        return true;
    }

    return false;
}

/**
 * @brief Try to open and add a single input device
 *
 * Attempts to open an input device file, initializes libevdev for it,
 * checks if it's a pointer device, and adds it to the input state's
 * device array if successful.
 *
 * @param input Pointer to input state structure
 * @param device_path Path to device file (e.g., "/dev/input/event0")
 * @return true if device was successfully added, false otherwise
 */
static bool try_add_device(struct input_state *input, const char *device_path) {
    if (input->device_count >= MAX_INPUT_DEVICES) {
        return false;
    }
    
    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;  // Silently skip inaccessible devices
    }
    
    struct libevdev *dev = NULL;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        close(fd);
        return false;
    }
    
    // Only add pointer devices
    if (!is_pointer_device(dev)) {
        libevdev_free(dev);
        close(fd);
        return false;
    }
    
    bool is_abs = libevdev_has_event_code(dev, EV_ABS, ABS_X);
    
    input->devices[input->device_count].fd = fd;
    input->devices[input->device_count].dev = dev;
    input->devices[input->device_count].is_absolute = is_abs;
    input->device_count++;
    
    return true;
}

/**
 * @brief Initializes kernel input device monitoring
 *
 * Scans /dev/input for event devices, identifies pointer devices using
 * libevdev, and sets up monitoring for them. Updates state->pointer_x/y
 * directly from kernel input events.
 *
 * @param state Pointer to global application state
 * @return true on success, false on failure (will log appropriate messages)
 *
 * @note This function requires read access to /dev/input/event* devices,
 *       typically available to members of the 'input' group.
 */
bool init_input(struct glwall_state *state) {
    LOG_DEBUG(state, "Initializing kernel input device monitoring...");
    
    // Allocate input state
    struct input_state *input = calloc(1, sizeof(struct input_state));
    if (!input) {
        LOG_ERROR("Failed to allocate input state");
        return false;
    }
    
    // Get screen dimensions from first output
    if (state->outputs) {
        input->screen_width = state->outputs->width;
        input->screen_height = state->outputs->height;
    } else {
        input->screen_width = 1920;  // Fallback
        input->screen_height = 1080;
    }
    
    // Scan /dev/input for event devices
    DIR *dir = opendir(INPUT_DEVICE_PATH);
    if (!dir) {
        LOG_WARN("Cannot open %s: %s (kernel input will not work)", 
                 INPUT_DEVICE_PATH, strerror(errno));
        free(input);
        return false;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Look for eventX devices
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }
        
        char device_path[256];
        snprintf(device_path, sizeof(device_path), "%s/%s", 
                 INPUT_DEVICE_PATH, entry->d_name);
        
        if (try_add_device(input, device_path)) {
            const char *name = libevdev_get_name(
                input->devices[input->device_count - 1].dev);
            LOG_INFO("Found pointer device: %s (%s)", entry->d_name, name);
        }
    }
    closedir(dir);
    
    if (input->device_count == 0) {
        LOG_WARN("No accessible pointer devices found in %s", INPUT_DEVICE_PATH);
        LOG_WARN("Kernel input capture requires read access (add user to 'input' group)");
        free(input);
        return false;
    }
    
    LOG_INFO("Kernel input initialized with %d pointer device(s)", input->device_count);
    
    // Store in state (using audio.impl as a template for opaque storage)
    // We'll add a proper field to state.h
    state->input_impl = input;
    
    return true;
}

/**
 * @brief Polls for kernel input events
 *
 * Performs non-blocking reads from all monitored input devices. Updates
 * state->pointer_x, state->pointer_y, and state->pointer_down based on
 * kernel input events. Should be called regularly (e.g., before each
 * render frame) to ensure responsive input handling.
 *
 * @param state Pointer to global application state
 *
 * @note This function is a no-op if state->input_impl is NULL.
 */
void poll_input_events(struct glwall_state *state) {
    if (!state->input_impl) {
        return;
    }
    
    struct input_state *input = (struct input_state *)state->input_impl;
    struct input_event ev;
    
    // Poll all devices
    for (int i = 0; i < input->device_count; i++) {
        struct input_device *device = &input->devices[i];
        
        while (1) {
            int rc = libevdev_next_event(device->dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
            
            if (rc == -EAGAIN) {
                break;  // No more events for this device
            }
            
            if (rc < 0) {
                continue;  // Error, skip
            }
            
            // Process pointer motion events
            if (ev.type == EV_REL) {
                if (ev.code == REL_X) {
                    state->pointer_x += ev.value;
                    // Clamp to screen bounds
                    if (state->pointer_x < 0) state->pointer_x = 0;
                    if (state->pointer_x >= input->screen_width) {
                        state->pointer_x = input->screen_width - 1;
                    }
                } else if (ev.code == REL_Y) {
                    state->pointer_y += ev.value;
                    if (state->pointer_y < 0) state->pointer_y = 0;
                    if (state->pointer_y >= input->screen_height) {
                        state->pointer_y = input->screen_height - 1;
                    }
                }
            } else if (ev.type == EV_ABS && device->is_absolute) {
                // Absolute coordinates (touchpad, tablet)
                const struct input_absinfo *abs_info;
                
                if (ev.code == ABS_X) {
                    abs_info = libevdev_get_abs_info(device->dev, ABS_X);
                    if (abs_info) {
                        // Normalize to screen coordinates
                        double norm = (double)(ev.value - abs_info->minimum) / 
                                     (abs_info->maximum - abs_info->minimum);
                        state->pointer_x = norm * input->screen_width;
                    }
                } else if (ev.code == ABS_Y) {
                    abs_info = libevdev_get_abs_info(device->dev, ABS_Y);
                    if (abs_info) {
                        double norm = (double)(ev.value - abs_info->minimum) / 
                                     (abs_info->maximum - abs_info->minimum);
                        state->pointer_y = norm * input->screen_height;
                    }
                }
            } else if (ev.type == EV_KEY) {
                // Mouse button events
                if (ev.code == BTN_LEFT) {
                    state->pointer_down = (ev.value != 0);
                    if (state->pointer_down) {
                        state->pointer_down_x = state->pointer_x;
                        state->pointer_down_y = state->pointer_y;
                    }
                }
            }
        }
    }
}

/**
 * @brief Cleans up input device resources
 *
 * Closes all monitored input device file descriptors, frees libevdev
 * structures, and deallocates the input state. After this function,
 * state->input_impl is set to NULL.
 *
 * @param state Pointer to global application state
 *
 * @note This function is a no-op if state->input_impl is NULL.
 */
void cleanup_input(struct glwall_state *state) {
    if (!state->input_impl) {
        return;
    }

    LOG_DEBUG(state, "Cleaning up kernel input devices...");
    
    struct input_state *input = (struct input_state *)state->input_impl;
    
    for (int i = 0; i < input->device_count; i++) {
        if (input->devices[i].dev) {
            libevdev_free(input->devices[i].dev);
        }
        if (input->devices[i].fd >= 0) {
            close(input->devices[i].fd);
        }
    }
    
    free(input);
    state->input_impl = NULL;
}
