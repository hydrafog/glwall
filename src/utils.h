/**
 * @file utils.h
 * @brief Utility functions and logging macros for GLWall
 * 
 * This module provides file I/O utilities, command-line option parsing,
 * and logging macros for error, info, and debug messages.
 */

#ifndef UTILS_H
#define UTILS_H

#include "state.h"

// Logging Macros

#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(state, fmt, ...) do { if ((state)->debug) fprintf(stdout, "[DEBUG] " fmt "\n", ##__VA_ARGS__); } while (0)

// Function Declarations

/**
 * @brief Reads entire file contents into memory
 * 
 * Allocates a buffer and reads the entire file into it. The returned
 * buffer is null-terminated and must be freed by the caller.
 * 
 * @param path Path to the file to read
 * @return Pointer to allocated buffer containing file contents, or NULL on error
 * 
 * @note Caller must free the returned buffer
 */
char *read_file(const char *path);

/**
 * @brief Parses command-line options
 * 
 * Parses command-line arguments and populates the state structure with
 * shader path and debug flag. Exits with error if required options are missing.
 * 
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @param state Pointer to state structure to populate
 * 
 * @post state->shader_path and state->debug are set based on arguments
 */
void parse_options(int argc, char *argv[], struct glwall_state *state);

#endif // UTILS_H