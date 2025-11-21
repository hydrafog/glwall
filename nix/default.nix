# default.nix
# GLWall Configuration
#
# This file enables and configures the GLWall shader wallpaper service.
# It imports the main module definition from ./module.nix and sets the
# shader path to the retrowave GLSL shader.

{ ... }:

{
  imports = [ ./module.nix ];

  # GLWall Service Configuration
  
  glwall = {
    enable = true;
    shaderPath = builtins.toString ../shaders/retrowave.glsl;
    texturePath = null;
  };
}
