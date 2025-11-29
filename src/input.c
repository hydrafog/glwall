#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "input.h"
#include "utils.h"

#define MAX_INPUT_DEVICES 16
#define INPUT_DEVICE_PATH "/dev/input"
#define GLWALL_DEFAULT_SCREEN_WIDTH 1920
#define GLWALL_DEFAULT_SCREEN_HEIGHT 1080

struct input_device {
    int fd;
    struct libevdev *dev;
    bool is_absolute;
};

struct input_state {
    struct input_device devices[MAX_INPUT_DEVICES];
    int device_count;
    int screen_width;
    int screen_height;
    bool use_hyprland_ipc;
    char hyprland_socket_path[256];
};

static bool is_absolute_pointer(struct libevdev *dev) {
    return libevdev_has_event_code(dev, EV_ABS, ABS_X) &&
           libevdev_has_event_code(dev, EV_ABS, ABS_Y);
}

static bool is_relative_pointer(struct libevdev *dev) {
    return libevdev_has_event_code(dev, EV_REL, REL_X) &&
           libevdev_has_event_code(dev, EV_REL, REL_Y);
}

static bool is_pointer_device(struct libevdev *dev) {
    return is_absolute_pointer(dev) || is_relative_pointer(dev);
}

 
static bool has_relative_device_for_hardware(struct input_state *input, struct libevdev *dev) {
    int vid = libevdev_get_id_vendor(dev);
    int pid = libevdev_get_id_product(dev);
    
    for (int i = 0; i < input->device_count; i++) {
        struct libevdev *existing = input->devices[i].dev;
        if (!input->devices[i].is_absolute &&
            libevdev_get_id_vendor(existing) == vid &&
            libevdev_get_id_product(existing) == pid) {
            return true;
        }
    }
    return false;
}

 
static bool has_absolute_device_for_hardware(struct input_state *input, struct libevdev *dev) {
    int vid = libevdev_get_id_vendor(dev);
    int pid = libevdev_get_id_product(dev);
    
    for (int i = 0; i < input->device_count; i++) {
        struct libevdev *existing = input->devices[i].dev;
        if (input->devices[i].is_absolute &&
            libevdev_get_id_vendor(existing) == vid &&
            libevdev_get_id_product(existing) == pid) {
            return true;
        }
    }
    return false;
}

static bool try_add_device(struct input_state *input, const char *device_path) {
    if (input->device_count >= MAX_INPUT_DEVICES) {
        return false;
    }

    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }

    struct libevdev *dev = NULL;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        close(fd);
        return false;
    }

    if (!is_pointer_device(dev)) {
        libevdev_free(dev);
        close(fd);
        return false;
    }

    bool is_abs = is_absolute_pointer(dev);
    
     
    if (!is_abs && has_absolute_device_for_hardware(input, dev)) {
        libevdev_free(dev);
        close(fd);
        return false;
    }

    input->devices[input->device_count].fd = fd;
    input->devices[input->device_count].dev = dev;
    input->devices[input->device_count].is_absolute = is_abs;
    input->device_count++;

    return true;
}

 
static bool query_hyprland_cursor(struct input_state *input, double *x, double *y) {
    if (!input->use_hyprland_ipc) {
        return false;
    }
    
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, input->hyprland_socket_path, sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return false;
    }
    
     
    const char *cmd = "cursorpos";
    if (write(sock, cmd, strlen(cmd)) < 0) {
        close(sock);
        return false;
    }
    
     
    char buf[64];
    ssize_t n = read(sock, buf, sizeof(buf) - 1);
    close(sock);
    
    if (n <= 0) {
        return false;
    }
    buf[n] = '\0';
    
     
    if (sscanf(buf, "%lf, %lf", x, y) != 2) {
        return false;
    }
    
    return true;
}

 
static bool init_hyprland_ipc(struct input_state *input) {
    const char *his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
    
    if (!his || !xdg_runtime) {
        return false;
    }
    
    snprintf(input->hyprland_socket_path, sizeof(input->hyprland_socket_path),
             "%s/hypr/%s/.socket.sock", xdg_runtime, his);
    
     
    double x, y;
    input->use_hyprland_ipc = true;
    if (!query_hyprland_cursor(input, &x, &y)) {
        input->use_hyprland_ipc = false;
        return false;
    }
    
    return true;
}

bool init_input(struct glwall_state *state) {
    LOG_DEBUG(state, "Input subsystem: kernel input device monitoring initialization commenced");

    struct input_state *input = calloc(1, sizeof(struct input_state));
    if (!input) {
        LOG_ERROR("Memory allocation failed: insufficient memory for input state");
        return false;
    }

    if (state->outputs) {
        input->screen_width = state->outputs->width;
        input->screen_height = state->outputs->height;
    } else {
        input->screen_width = GLWALL_DEFAULT_SCREEN_WIDTH;
        input->screen_height = GLWALL_DEFAULT_SCREEN_HEIGHT;
    }

     
    if (init_hyprland_ipc(input)) {
        LOG_INFO("Input subsystem: Hyprland IPC initialized for cursor tracking");
        state->input_impl = input;
        
         
        double x, y;
        if (query_hyprland_cursor(input, &x, &y)) {
            state->pointer_x = x;
            state->pointer_y = y;
        }
        return true;
    }

     
    DIR *dir = opendir(INPUT_DEVICE_PATH);
    if (!dir) {
        LOG_WARN("Input subsystem warning: unable to access %s (errno: %s) - kernel input disabled", INPUT_DEVICE_PATH,
                 strerror(errno));
        free(input);
        return false;
    }

     
    
     
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        char device_path[PATH_MAX];
        int ret = snprintf(device_path, sizeof(device_path), "%s/%s", INPUT_DEVICE_PATH, entry->d_name);
        if (ret < 0 || ret >= (int)sizeof(device_path)) {
            continue;
        }

        int fd = open(device_path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        
        struct libevdev *dev = NULL;
        if (libevdev_new_from_fd(fd, &dev) < 0) {
            close(fd);
            continue;
        }
        
        if (is_relative_pointer(dev) && input->device_count < MAX_INPUT_DEVICES) {
            input->devices[input->device_count].fd = fd;
            input->devices[input->device_count].dev = dev;
            input->devices[input->device_count].is_absolute = false;
            input->device_count++;
            LOG_INFO("Input subsystem: relative pointer device detected (%s: %s)", 
                     entry->d_name, libevdev_get_name(dev));
        } else {
            libevdev_free(dev);
            close(fd);
        }
    }
    
     
    rewinddir(dir);
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        char device_path[PATH_MAX];
        int ret = snprintf(device_path, sizeof(device_path), "%s/%s", INPUT_DEVICE_PATH, entry->d_name);
        if (ret < 0 || ret >= (int)sizeof(device_path)) {
            continue;
        }

        int fd = open(device_path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        
        struct libevdev *dev = NULL;
        if (libevdev_new_from_fd(fd, &dev) < 0) {
            close(fd);
            continue;
        }
        
         
        if (is_absolute_pointer(dev) && !is_relative_pointer(dev) && 
            !has_relative_device_for_hardware(input, dev) &&
            input->device_count < MAX_INPUT_DEVICES) {
            input->devices[input->device_count].fd = fd;
            input->devices[input->device_count].dev = dev;
            input->devices[input->device_count].is_absolute = true;
            input->device_count++;
            LOG_INFO("Input subsystem: absolute pointer device detected (%s: %s)", 
                     entry->d_name, libevdev_get_name(dev));
        } else {
            libevdev_free(dev);
            close(fd);
        }
    }
    closedir(dir);

    if (input->device_count == 0) {
        LOG_WARN("Input subsystem warning: no accessible pointer devices found in %s", INPUT_DEVICE_PATH);
        LOG_WARN("Input subsystem warning: permission denied - kernel input requires read access to input devices (add user to 'input' group)");
        free(input);
        return false;
    }

    LOG_INFO("Input subsystem initialization: kernel input enabled with %d pointer device(s)", input->device_count);

    state->input_impl = input;
    
     
    state->pointer_x = input->screen_width / 2.0;
    state->pointer_y = input->screen_height / 2.0;

    return true;
}

void poll_input_events(struct glwall_state *state) {
    if (!state->input_impl) {
        return;
    }

    struct input_state *input = (struct input_state *)state->input_impl;
    
     
    if (input->use_hyprland_ipc) {
        double x, y;
        if (query_hyprland_cursor(input, &x, &y)) {
            state->pointer_x = x;
            state->pointer_y = y;
        }
        return;
    }
    
     
    struct input_event ev;

    for (int i = 0; i < input->device_count; i++) {
        struct input_device *device = &input->devices[i];

        while (1) {
            int rc = libevdev_next_event(device->dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

            if (rc == -EAGAIN) {
                break;
            }

            if (rc < 0) {
                continue;
            }

            if (ev.type == EV_REL) {
                if (ev.code == REL_X) {
                    state->pointer_x += ev.value;

                    if (state->pointer_x < 0)
                        state->pointer_x = 0;
                    if (state->pointer_x >= input->screen_width) {
                        state->pointer_x = input->screen_width - 1;
                    }
                    LOG_DEBUG(state, "Input event: REL_X motion detected (delta: %d, position: %.1f)", ev.value, state->pointer_x);
                } else if (ev.code == REL_Y) {
                    state->pointer_y += ev.value;
                    if (state->pointer_y < 0)
                        state->pointer_y = 0;
                    if (state->pointer_y >= input->screen_height) {
                        state->pointer_y = input->screen_height - 1;
                    }
                }
            } else if (ev.type == EV_ABS && device->is_absolute) {

                const struct input_absinfo *abs_info;

                if (ev.code == ABS_X) {
                    abs_info = libevdev_get_abs_info(device->dev, ABS_X);
                    if (abs_info) {

                        double denom = (double)(abs_info->maximum - abs_info->minimum);
                        if (denom <= 0.0) {
                            LOG_WARN("Input subsystem: invalid abs range (max == min) for ABS_X");
                        } else {
                            double norm = (double)(ev.value - abs_info->minimum) / denom;
                            state->pointer_x = norm * input->screen_width;
                        }
                    }
                } else if (ev.code == ABS_Y) {
                    abs_info = libevdev_get_abs_info(device->dev, ABS_Y);
                    if (abs_info) {
                        double denom = (double)(abs_info->maximum - abs_info->minimum);
                        if (denom <= 0.0) {
                            LOG_WARN("Input subsystem: invalid abs range (max == min) for ABS_Y");
                        } else {
                            double norm = (double)(ev.value - abs_info->minimum) / denom;
                            state->pointer_y = norm * input->screen_height;
                        }
                    }
                }
            } else if (ev.type == EV_KEY) {

                if (ev.code == BTN_LEFT) {
                    state->pointer_down = (ev.value != 0);
                    if (state->pointer_down) {
                        state->pointer_down_x = state->pointer_x;
                        state->pointer_down_y = state->pointer_y;
                    }
                    LOG_DEBUG(state, "Input event: BTN_LEFT detected (state: %d, position: %.1f, %.1f)", ev.value,
                              state->pointer_x, state->pointer_y);
                }
            }
        }
    }
}

void cleanup_input(struct glwall_state *state) {
    if (!state->input_impl) {
        return;
    }

    LOG_DEBUG(state, "Input subsystem cleanup initiated");

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
