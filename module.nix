{ config, pkgs, lib, ... }:

let
  cfg = config.glwall;

  glwallPackage = pkgs.stdenv.mkDerivation {
    pname = "glwall";
    version = "1.0.0";
    src = ./.;

    nativeBuildInputs = with pkgs; [
      pkg-config
      wayland-scanner
    ];

    buildInputs = with pkgs; [
      gcc
      gnumake
      glew
      libGL
      libpng
      wayland
      wayland-protocols
      wlr-protocols
      egl-wayland
      pulseaudio
      libevdev
    ];

    WAYLAND_PROTOCOLS_DIR = "${pkgs.wayland-protocols}/share/wayland-protocols";
    WLR_PROTOCOLS_DIR = "${pkgs.wlr-protocols}/share/wlr-protocols";

    buildPhase = ''
      cd src
      make clean
      make \
        EXTRA_CFLAGS="-I${pkgs.libevdev}/include/libevdev-1.0" \
        LDFLAGS="-lGL -lGLEW -lEGL -lwayland-client -lwayland-egl -lm -lpulse-simple -lpulse -levdev -lpng"
    '';

    installPhase = ''
      mkdir -p $out/bin
      cp ./glwall $out/bin/
      mkdir -p $out/share/glwall/shaders
      cp -r ../shaders/* $out/share/glwall/shaders/
    '';

    meta = with pkgs.lib; {
      description = "A multi-monitor GLSL shader wallpaper renderer for Wayland";
      homepage = "https://github.com/hydrafog/glwall";
      license = licenses.unlicense;
      maintainers = [ ];
      platforms = platforms.linux;
    };
  };
in
{
  options.glwall = {
    enable = lib.mkEnableOption "GLWall shader wallpaper renderer";

    shaderPath = lib.mkOption {
      type = lib.types.str;
      description = "Path to the GLSL fragment shader file";
      default = "${glwallPackage}/share/glwall/shaders/retrowave.glsl";
    };

    imagePath = lib.mkOption {
      type = lib.types.nullOr lib.types.str;
      description = "Optional image path for --image";
      default = null;
    };

    debug = lib.mkOption {
      type = lib.types.bool;
      default = false;
      description = "Enable debug logging";
    };

    package = lib.mkOption {
      type = lib.types.package;
      default = glwallPackage;
      description = "GLWall package to use";
    };

    powerMode = lib.mkOption {
      type = lib.types.enum [ "full" "throttled" "paused" ];
      default = "full";
      description = "Rendering power policy";
    };

    mouseOverlay = lib.mkOption {
      type = lib.types.enum [ "none" "edge" "full" ];
      default = "none";
      description = "Mouse overlay mode";
    };

    mouseOverlayHeight = lib.mkOption {
      type = lib.types.int;
      default = 32;
      description = "Height of edge strip when mouseOverlay = 'edge'";
    };

    audio = {
      enable = lib.mkOption {
        type = lib.types.bool;
        default = true;
        description = "Enable audio-reactive shaders";
      };

      source = lib.mkOption {
        type = lib.types.enum [ "pulseaudio" "fake" "none" ];
        default = "pulseaudio";
        description = "Audio backend to use";
      };
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];

    systemd.user.services.glwall = {
      description = "GLWall shader wallpaper";
      wantedBy = [ "graphical-session.target" ];
      after = [ "graphical-session.target" ];

      serviceConfig =
        let
          powerArg = "--power-mode ${cfg.powerMode}";
          mouseArgs = "--mouse-overlay ${cfg.mouseOverlay} --mouse-overlay-height ${toString cfg.mouseOverlayHeight}";
          audioArgs = if cfg.audio.enable then
            "--audio --audio-source ${cfg.audio.source}"
          else
            "--no-audio --audio-source none";
          debugArg = lib.optionalString cfg.debug "--debug";
          imageArg = lib.optionalString (cfg.imagePath != null) "--image ${cfg.imagePath}";
        in
        {
          ExecStart = "${cfg.package}/bin/glwall -s ${cfg.shaderPath} ${imageArg} ${debugArg} ${powerArg} ${mouseArgs} ${audioArgs}";
          Restart = "on-failure";
        };
    };
  };
}
