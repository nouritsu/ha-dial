{
  description = "M5Stack Dial firmware for Home Assistant or Tapo lights";

  inputs = {
    esp-dev.url = "github:mirrexagon/nixpkgs-esp-dev";
    # the one esp-dev builds ESP-IDF's Python packages against
    nixpkgs.follows = "esp-dev/nixpkgs";
  };

  outputs = {
    self,
    nixpkgs,
    esp-dev,
  }: let
    # the formatters come from devenv.lock, so the formatting check runs the
    # versions the git hooks do
    devenv = input: fetchTree (builtins.fromJSON (builtins.readFile ./devenv.lock)).nodes.${input}.locked;

    perSystem = system: let
      pkgs = import nixpkgs {
        inherit system;
        overlays = [esp-dev.overlays.default];
        # as esp-dev's own flake: its esptool 4.9 needs ecdsa
        config.permittedInsecurePackages = ["python3.13-ecdsa-0.19.1"];
      };
      esp-idf = pkgs.callPackage ./nix/esp-idf.nix {};
      # nix build .#ha or .#tapo; nix run .#ha flashes it with ./secrets.ini
      firmware = backend:
        pkgs.callPackage ./nix/firmware.nix {
          inherit esp-idf backend;
          version = self.shortRev or self.dirtyShortRev;
        };
      treefmt = (import (devenv "treefmt-nix")).evalModule (import (devenv "nixpkgs") {inherit system;}) {
        imports = ["${devenv "conf-nixos"}/templates/base/treefmt.nix" ./nix/treefmt.nix];
        projectRootFile = "flake.nix";
      };
    in rec {
      packages = {
        ha = firmware "ha";
        tapo = firmware "tapo";
        default = packages.ha;
      };
      # nix fmt
      formatter = treefmt.config.build.wrapper;
      # nix flake check, which CI runs on every push
      checks = {
        inherit (packages) ha tapo;
        formatting = treefmt.config.build.check self;
        flasher = pkgs.callPackage ./nix/flasher-test.nix {
          inherit esp-idf;
          inherit (packages.ha) python;
        };
      };
    };
    systems = nixpkgs.lib.genAttrs ["x86_64-linux" "aarch64-linux"];
  in {
    packages = systems (system: (perSystem system).packages);
    formatter = systems (system: (perSystem system).formatter);
    checks = systems (system: (perSystem system).checks);
  };
}
