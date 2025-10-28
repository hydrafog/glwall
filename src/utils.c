/**
 * @file utils.c
 * @brief Implementation of utility functions for GLWall
 * 
 * This file implements file I/O and command-line parsing utilities.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include "utils.h"

// Public Function Implementations

/**
 * @brief Reads entire file contents into memory
 * 
 * Opens the file, determines its size, allocates a buffer, reads the
 * contents, and returns the null-terminated buffer.
 * 
 * @param path Path to the file to read
 * @return Pointer to allocated buffer, or NULL on error
 */
char *read_file(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        LOG_ERROR("Failed to open file '%s': %s", path, strerror(errno));
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    if (length < 0) {
        LOG_ERROR("Failed to determine file size for '%s': %s", path, strerror(errno));
        fclose(file);
        return NULL;
    }
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (!buffer) {
        LOG_ERROR("Failed to allocate memory for file contents");
        fclose(file);
        return NULL;
    }

    // Check the return value of fread to ensure complete read.
    size_t read_length = fread(buffer, 1, length, file);
    if (read_length != (size_t)length) {
        LOG_ERROR("Failed to read full file '%s'. Read %zu bytes, expected %ld.", path, read_length, length);
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[length] = '\0';
    fclose(file);
    return buffer;
}

/**
 * @brief Parses command-line options
 * 
 * Processes -s/--shader and -d/--debug options. Exits with error message
 * if required shader path is not provided.
 * 
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @param state Pointer to state structure to populate
 */
void parse_options(int argc, char *argv[], struct glwall_state *state) {
    struct option long_options[] = {
        {"shader", required_argument, 0, 's'},
        {"debug", no_argument, 0, 'd'},
        {0, 0, 0, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "s:d", long_options, NULL)) != -1) {
        switch (c) {
            case 's':
                state->shader_path = optarg;
                break;
            case 'd':
                state->debug = true;
                break;
            default:
                fprintf(stderr, "Usage: %s -s <shader.frag> [--debug]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if (!state->shader_path) {
        LOG_ERROR("Shader path is required. Use -s /path/to/shader.frag");
        exit(EXIT_FAILURE);
    }
}