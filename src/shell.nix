# /etc/dotfiles/nixos/shared/modules/glwall/src/shell.nix
{
  pkgs ? import <nixpkgs> { },
}:

pkgs.mkShell {
  buildInputs = with pkgs; [
    # C compiler and build tools
    gcc
    gnumake
    pkg-config

    # OpenGL libraries
    glew
    libGL

    # Wayland and EGL libraries for native windowing
    wayland
    wayland-scanner
    wayland-protocols
    wlr-protocols
    egl-wayland
  ];

  # ADD THIS LINE:
  # This exports the correct path to the wlr-protocols directory into the shell environment.
  # The Makefile will use this variable directly.
  # Export protocol directories for the Makefile
  WAYLAND_PROTOCOLS_DIR = "${pkgs.wayland-protocols}/share/wayland-protocols";
  WLR_PROTOCOLS_DIR = "${pkgs.wlr-protocols}/share/wlr-protocols";
}
