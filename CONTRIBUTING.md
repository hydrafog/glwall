# Contributing to GLWall

<!-- Context Type: Governance & Workflow -->
<!-- Scope: Development workflow, standards, and contribution guidelines. -->

## Table of Contents
- [1. Getting Started](#1-getting-started)
- [2. Development Workflow](#2-development-workflow)
- [3. Code Style](#3-code-style)
- [4. Reporting Issues](#4-reporting-issues)

## 1. Getting Started

Thank you for your interest in contributing to GLWall! We welcome contributions from the community.

1.  **Fork the repository** on GitHub.
2.  **Clone your fork** locally.
3.  **Set up the environment**:
    *   We recommend using **Nix** to set up the development environment.
    *   Run `nix develop` to get a shell with all dependencies.
    *   Alternatively, install dependencies listed in `flake.nix` manually.

## 2. Development Workflow

1.  **Create a branch** for your feature or bugfix: `git checkout -b feature/my-awesome-feature`.
2.  **Make your changes**.
3.  **Test your changes**:
    *   Build: `cd src && make`
    *   Run: `./glwall -s ../shaders/template.glsl --debug`
4.  **Commit your changes** using [Conventional Commits](https://www.conventionalcommits.org/):
    *   `feat: add new shader`
    *   `fix: resolve memory leak`
    *   `docs: update readme`
    *   `style: fix indentation`
    *   `refactor: simplify render loop`
5.  **Push to your fork** and submit a **Pull Request**.

## 3. Code Style

*   **C Code**: Follow the existing style (K&R style mostly). Keep functions small and focused.
*   **Nix**: Format with `nixpkgs-fmt` if possible.
*   **Documentation**: Use Markdown with strict adherence to the toolkit standards.

## 4. Reporting Issues

If you find a bug or have a feature request, please open an issue on the GitHub repository. Provide as much detail as possible, including:
*   Your Wayland compositor (Hyprland, Sway, etc.).
*   GPU and driver version.
*   Logs (run with `--debug`).
