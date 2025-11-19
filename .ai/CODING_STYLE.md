# Coding Style & Standards

<!-- Context Type: Development Standards -->
<!-- Scope: Code generation rules, formatting, and architectural constraints. -->

## Table of Contents
- [1. General Principles](#1-general-principles)
- [2. C Standards](#2-c-standards)
- [3. Nix Standards](#3-nix-standards)

## 1. General Principles

*   **Clarity over Cleverness:** Write code that is easy to read and maintain.
*   **No Magic Numbers:** Use `#define` constants or enums.
*   **Comments:** Document complex logic and public API functions (Doxygen style).

## 2. C Standards

*   **Standard:** C99 or C11.
*   **Formatting:** K&R style. 4 spaces indentation. No tabs.
*   **Types:**
    *   Use `<stdint.h>` types (`int32_t`, `uint64_t`, `size_t`) for fixed-width or size-dependent logic.
    *   Use `bool` from `<stdbool.h>` for boolean values.
*   **Memory Management:**
    *   Always check `malloc`/`calloc` return values.
    *   Free resources in reverse order of allocation.
    *   Use `goto cleanup` pattern for error handling in complex functions.
*   **Naming:**
    *   `snake_case` for variables and functions.
    *   `SCREAMING_SNAKE_CASE` for macros and constants.
    *   `struct glwall_state` (namespaced structs).
*   **State Management:**
    *   Avoid global variables.
    *   Pass `struct glwall_state *` context to functions that need access to shared state.
*   **Headers:**
    *   Use `#pragma once` for header guards.
    *   Include local headers with `""` and system headers with `<>`.
*   **Error Handling:**
    *   Functions should return `bool` (true=success, false=fail) or `int` (0=success, <0=error).
    *   Log errors immediately using `LOG_ERROR` before returning failure.

## 3. Nix Standards

*   **Formatting:** Use `nixpkgs-fmt`.
*   **Flakes:** Use Flakes for reproducibility.
*   **Inputs:** Pin inputs to specific commits or tags where possible.
