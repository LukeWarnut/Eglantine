# NixOS module for Eglantine: installs the emulator and, with it, the udev rules
# (dist/72-eglantine-input.rules) that give the seated user raw hidraw access to the
# pads. That is what HD Rumble needs over Bluetooth: the kernel's hid-nintendo keeps
# the pad's hidraw node root-only, so SDL cannot claim the controller there and every
# raw rumble packet is refused. With the rules installed, SDL owns the pad and the
# engine's packets reach it.
#
# Usage:
#   inputs.eglantine.url = "github:Octelly/Eglantine";
#   nixpkgs.overlays = [ inputs.eglantine.overlays.default ];
#   imports = [ inputs.eglantine.nixosModules.default ];
#   programs.eglantine.enable = true;
#
# The default package is `pkgs.eglantine`, which the overlay provides. Set
# `programs.eglantine.package` to something else (the PGO build, a fork) to taste.
{
  config,
  lib,
  pkgs,
  ...
}:

let
  cfg = config.programs.eglantine;
in
{
  options.programs.eglantine = {
    enable = lib.mkEnableOption "Eglantine, installing the emulator and its controller udev rules";

    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.eglantine;
      defaultText = lib.literalExpression "pkgs.eglantine";
      description = "The eglantine derivation; it must install lib/udev/rules.d.";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];

    services.udev.packages = [ cfg.package ];
  };
}
