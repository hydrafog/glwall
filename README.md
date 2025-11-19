# GLWall

<!-- Context Type: Project Root -->
<!-- Scope: High-level overview, quick start, and documentation index. -->

[![License](https://img.shields.io/badge/License-Unlicense-blue.svg)](./LICENSE)
[![Status](https://img.shields.io/badge/Status-Active-green.svg)]()

**GLSL Shader Wallpaper Renderer for Wayland**

GLWall is a high-performance wallpaper renderer for Wayland compositors that renders GLSL shaders directly to the desktop background. It supports multi-monitor setups, audio reactivity, and mouse interaction.

## Documentation Index

### Operations & Deployment
*   **[Installation Guide](.documentation/installation.md)**
    Prerequisites, Nix installation, and manual build steps.
*   **[Configuration Reference](.documentation/configuration.md)**
    CLI arguments, NixOS module options, and shader uniforms.

### System Internals
*   **[Architecture Overview](.documentation/architecture.md)**
    High-level design, subsystems, and data flow.
*   **[Project Structure](.documentation/structure.md)**
    Directory map and code organization.

### Development Standards
*   **[Testing Strategy](.documentation/testing.md)**
    Manual verification steps and future testing plans.
*   **[Roadmap](.documentation/roadmap.md)**
    Future plans and technical debt.

## Community & Governance
*   **[CONTRIBUTING.md](CONTRIBUTING.md)** - Development workflow, standards, and contribution guidelines.

## AI & Agent Context
*   **[Coding Style](.ai/CODING_STYLE.md)** - C and Nix coding standards.
*   **[Context](.ai/CONTEXT.md)** - High-level project context.
*   **[Glossary](.ai/GLOSSARY.md)** - Domain-specific terminology.

## Quick References

*   **Entry Point:** `src/main.c`
*   **Build:** `make` (in `src/`)

## License

The Unlicense - see [LICENSE](./LICENSE).
