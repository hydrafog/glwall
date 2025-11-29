# Coding Style & Standards
<!-- Context Type: Development Standards -->
<!-- Scope: Code generation rules, formatting, and architectural constraints. -->

## Table of Contents
- [1. General Principles](#1-general-principles)
- [2. C Standards](#2-c-standards)
- [3. Nix Standards](#3-nix-standards)
- [4. Version Control & Commits](#4-version-control--commits)

-----

## 1. General Principles

*   **Clarity over Cleverness:** Write code that is easy to read and maintain. If an optimization makes code unreadable, document it extensively.
*   **Fail Fast:** Check for errors immediately. Do not propagate invalid state deep into the call stack.
*   **No Magic Numbers:** Use `#define` macros for configuration or `enum` for related states.
    ```c
    // Bad
    if (buffer_size > 1024) ...

    // Good
    #define MAX_BUFFER_SIZE 1024
    if (buffer_size > MAX_BUFFER_SIZE) ...
    ```
*   **Comments & Documentation:**
    *   **No Comments:** Do not use comments in the code. The code should be self-explanatory. If logic is complex, refactor it to be clearer.
    *   **Documentation:** Documentation should live in separate markdown files, not in the source code.

-----

## 2. C Standards

### 2.1 Language & Formatting

*   **Standard:** C11 (Use C99 features freely, but prefer C11 for atomics/threads if needed).
*   **Formatting:** K&R style. **4 spaces** indentation. No tabs.
*   **Line Length:** Soft limit of 80 characters, hard limit of 100.
*   **Braces:** Open braces on the same line.
    ```c
    if (condition) {
        action();
    } else {
        other_action();
    }
    ```

### 2.2 Types & Variables

*   **Fixed Width:** Use `<stdint.h>` types (`int32_t`, `uint64_t`, `uint8_t`) for logic where size matters (protocols, hardware registers). Use `int` or `size_t` for loops and array indexing.
*   **Booleans:** Use `bool`, `true`, and `false` from `<stdbool.h>`. Do not use `1`/`0` or integers for logic.
*   **Const Correctness:** Aggressively use `const`. If a pointer target isn't modified, mark it `const`.
    ```c
    void process_data(const uint8_t *data, size_t len);
    ```
*   **Pointer Syntax:** The `*` binds to the variable name, not the type.
    *   *Good:* `int *ptr;`
    *   *Bad:* `int* ptr;`

### 2.3 Naming Conventions

*   **Variables/Functions:** `snake_case`.
*   **Constants/Macros:** `SCREAMING_SNAKE_CASE`.
*   **Structs/Enums:** `snake_case` with project namespace prefix.
    *   *Example:* `struct glwall_state`, `enum glwall_error`.
*   **Private Functions:** Static functions in `.c` files should not have a prefix, or use `_` prefix if name collision is likely.

### 2.4 Memory & Resource Management

*   **Allocation:** Always check `malloc`/`calloc` return values.
*   **Cleanup:** Free resources in the **reverse order** of allocation.
*   **Error Handling Pattern:** Use the `goto cleanup` pattern to avoid code duplication in complex functions.

```c
int process_file(const char *filename) {
    int rc = -1;
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char *buf = malloc(1024);
    if (!buf) {
        LOG_ERROR("OOM");
        goto cleanup_file; // Jump to cleanup
    }

    if (do_work(f, buf) < 0) {
        goto cleanup_buf; // Jump to cleanup
    }

    rc = 0; // Success

cleanup_buf:
    free(buf);
cleanup_file:
    fclose(f);
    return rc;
}
```

### 2.5 State & Architecture

*   **No Globals:** Avoid global state variables.
*   **Context Passing:** Pass a context struct (`struct glwall_state *ctx`) to functions requiring shared state.
*   **Header Guards:** Use `#pragma once`.
*   **Includes:**
    *   Local: `#include "my_module.h"`
    *   System: `#include <stdio.h>`

-----

## 3. Nix Standards

*   **Formatting:** Code must pass `nixpkgs-fmt`.
*   **Reproducibility:** Always use Flakes. Do not rely on `NIX_PATH` channels.
*   **Inputs:**
    *   Pin inputs to specific Git commits or tags in `flake.nix`.
    *   Avoid `follow` unless necessary to reduce closure size.
*   **Syntax:**
    *   Use `inherit` to reduce repetition: `{ inherit (pkgs) stdenv fetchFromGitHub; }`.
    *   Use `let ... in` blocks for local variables at the top of the expression.
*   **Lists:** Multi-line lists must end with a semicolon/bracket on a new line.
    ```nix
    # Good
    buildInputs = [
      pkgs.openssl
      pkgs.curl
    ];
    ```

-----

## 4. Version Control & Commits

*   **Format:** Use **Conventional Commits** format.
    *   `type(scope): description`
    *   *Types:* `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`.
    *   *Example:* `fix(parser): handle null terminator in payload buffer`
*   **Granularity:** Atomic commits. One logical change per commit.
*   **History:** Do not merge broken code. Squashing is permitted for PRs, but preserve history for distinct logical steps.