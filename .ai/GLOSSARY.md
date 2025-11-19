# Glossary

<!-- Context Type: Domain Language -->
<!-- Scope: Definitions of domain-specific terms. -->

## Table of Contents
- [1. Wayland Terms](#1-wayland-terms)
- [2. Graphics Terms](#2-graphics-terms)

## 1. Wayland Terms

*   **Compositor:** The display server (e.g., Hyprland, Sway) that manages windows and inputs.
*   **Layer Shell:** A Wayland protocol (`zwlr_layer_shell_v1`) that allows clients to create surfaces at specific Z-orders (background, panel, overlay).
*   **Surface:** A rectangular area of pixels managed by a client.
*   **Seat:** A collection of input devices (keyboard, mouse, touch).

## 2. Graphics Terms

*   **EGL:** Interface between Khronos rendering APIs (like OpenGL) and the underlying native platform window system.
*   **Fragment Shader:** A GPU program that calculates the color of each pixel.
*   **Uniform:** A global variable passed from the CPU to the shader (e.g., time, resolution).
*   **VBO (Vertex Buffer Object):** GPU memory buffer storing vertex data.
