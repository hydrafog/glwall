# module.nix
# GLWall NixOS Module
#
# This module provides a NixOS service for GLWall, a multi-monitor GLSL
# shader-based dynamic wallpaper renderer. It builds from local source with
# Wayland compositor integration and OpenGL rendering support.
#
# The module defines options under `glwall.*` for enabling the service and
# configuring shader paths. When enabled, it installs the glwall package
# to the system environment.

{
  config,
  pkgs,
  lib,
  ...
}:

let
  # Package Derivation
  
  # GLWall package derivation for multi-monitor shader wallpapers.
  # Builds with Wayland layer-shell protocol and OpenGL 3.3 core profile.
  glwallPackage = pkgs.stdenv.mkDerivation {
    pname = "glwall";
    version = "1.0.0";

    src = ./.;

    # Build Dependencies
    
    # OpenGL and Wayland development libraries required for compilation.
    buildInputs = with pkgs; [
      # Build toolchain
      gcc
      gnumake
      pkg-config

      # OpenGL and graphics libraries
      glew
      libGL

      # Wayland compositor integration
      wayland
      wayland-scanner
      wayland-protocols
      wlr-protocols
      egl-wayland
    ];

    # Build Environment
    
    # Protocol directory environment variables for Wayland build.
    # These paths are required by the Makefile to locate protocol XML files.
    WAYLAND_PROTOCOLS_DIR = "${pkgs.wayland-protocols}/share/wayland-protocols";
    WLR_PROTOCOLS_DIR = "${pkgs.wlr-protocols}/share/wlr-protocols";

    # Build Phase
    
    # Build GLWall binary with required libraries.
    # Explicitly specify LDFLAGS to ensure all dependencies are linked.
    buildPhase = ''
      cd src
      make clean
      make LDFLAGS="-lGL -lGLEW -lEGL -lwayland-client -lwayland-egl -lm"
    '';

    # Install Phase
    
    # Install binary and shader resources to output directory.
    installPhase = ''
      mkdir -p $out/bin
      cp ./glwall $out/bin/
      mkdir -p $out/share/glwall
      cp -r ../shaders $out/share/glwall/
    '';

    # Package Metadata
    
    meta = with lib; {
      description = "A multi-monitor GLSL shader wallpaper renderer";
      license = licenses.mit;
      maintainers = [ maintainers.hyperfog ];
      platforms = platforms.linux;
    };
  };
in
{
  # Module Options
  
  # Configuration options for GLWall shader wallpaper service.
  options = {
    glwall.enable = lib.mkOption {
      type = lib.types.bool;
      description = "Enable GLWall shader wallpaper renderer.";
      default = false;
    };

    glwall.shaderPath = lib.mkOption {
      type = lib.types.str;
      description = "Path to the GLSL fragment shader file.";
      default = "/path/to/shader.glsl";
    };

    glwall.texturePath = lib.mkOption {
      type = lib.types.nullOr lib.types.str;
      description = "Optional texture image path for shader input.";
      default = null;
    };

    glwall.debug = lib.mkOption {
      type = lib.types.bool;
      description = "Enable debug logging.";
      default = false;
    };

    glwall.package = lib.mkOption {
      type = lib.types.package;
      readOnly = true;
      default = glwallPackage;
      description = "GLWall package for system installation.";
    };
  };

  # Module Configuration
  
  # Install GLWall package to system environment when enabled.
  config = lib.mkIf config.glwall.enable {
    environment.systemPackages = [ config.glwall.package ];

    systemd.user.services.glwall = {
      description = "GLWall - Shader Wallpaper Daemon";
      wantedBy = [ "graphical-session.target" ];
      partOf = [ "graphical-session.target" ];
      serviceConfig = {
        ExecStart = "${config.glwall.package}/bin/glwall -s ${config.glwall.shaderPath} ${lib.optionalString config.glwall.debug "--debug"} ${lib.optionalString (config.glwall.texturePath != null) "-i ${config.glwall.texturePath}"}";
        Restart = "always";
        RestartSec = "5s";
      };
    };
  };
}
