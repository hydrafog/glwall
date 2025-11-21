# default.nix
# Compatibility shim for non-flake Nix users
# This file provides backward compatibility by importing the actual
# configuration from the nix/ subdirectory.

import ./nix/default.nix
