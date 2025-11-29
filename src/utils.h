#pragma once

#include "state.h"

#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) fprintf(stderr, "[WARN ] " fmt "\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) fprintf(stdout, "[INFO ] " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(state, fmt, ...)                                                                 \
    do {                                                                                           \
        if ((state)->debug)                                                                        \
            fprintf(stdout, "[DEBUG] " fmt "\n", ##__VA_ARGS__);                                   \
    } while (0)

char *read_file(const char *path);

void parse_options(int argc, char *argv[], struct glwall_state *state);
