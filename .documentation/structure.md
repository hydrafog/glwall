# Code Structure & Organization

<!-- Context Type: Directory Map & Design Patterns -->
<!-- Scope: File hierarchy, module responsibilities, architectural invariants. -->

## Table of Contents
- [1. Directory Hierarchy](#1-directory-hierarchy)
- [2. Key Files](#2-key-files)

## 1. Directory Hierarchy

The codebase follows a flat C structure in `src/` with separation of concerns by file.

```text
glwall/
├── .documentation/     # Context Knowledge Graph.
├── scripts/            # Helper scripts.
│   ├── list-audio-sources.sh # List PulseAudio sources.
│   ├── remove_comments.py    # Documentation maintenance.
│   └── test-local.sh         # Local testing helper.
├── shaders/            # Example GLSL shaders.
├── src/                # Source code.
│   ├── main.c          # Entry point and loop.
│   ├── wayland.c       # Wayland protocol handling.
│   ├── egl.c           # EGL context management.
│   ├── opengl.c        # OpenGL rendering logic.
│   ├── audio.c         # Audio capture and processing.
│   ├── input.c         # Input handling (libevdev).
│   ├── utils.c         # File I/O and helpers.
│   └── *.h             # Header files.
├── flake.nix           # Nix build definition.
└── Makefile            # Manual build definition.
```

## 2. Key Files

*   **`src/state.h`**: Defines `struct glwall_state`, the central data structure passed around to all subsystems.
*   **`src/wayland.c`**: Handles the complexity of the Wayland registry and Layer Shell protocol.
*   **`src/opengl.c`**: Contains the hot render path.
