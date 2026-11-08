{
  pkgs,
  config,
  ...
}: let
  mdi = pkgs.fetchzip {
    url = "https://registry.npmjs.org/@mdi/font/-/font-7.4.47.tgz";
    hash = "sha256-dXxTVxHyTsPbF8DbeS43MJ9lKfk1CnYh87T8Y/F592w=";
  };
in {
  packages = [
    # FHS-wrapped, so the xtensa toolchain it downloads runs on NixOS
    pkgs.platformio
    # uncached on devenv-nixpkgs, and its test suite takes the better part of an hour
    (pkgs.esptool.overridePythonAttrs {doCheck = false;})
    pkgs.tio
    # lv_font_conv, through npx
    pkgs.nodejs
    # tools/dial.py: screenshots and remote input over the serial console;
    # tools/flash.py: secrets.ini as an NVS partition image
    (pkgs.python3.withPackages (ps: [ps.pyserial (ps.callPackage ./nix/nvs-partition-gen.nix {})]))
  ];

  env = {
    # toolchains, frameworks and libraries stay in .devenv/state
    PLATFORMIO_CORE_DIR = "${config.devenv.state}/platformio";
    ROBOTO_DIR = "${pkgs.roboto}/share/fonts/truetype";
    MDI_TTF = "${mdi}/fonts/materialdesignicons-webfont.ttf";
  };

  # clangd reads compile_commands.json from `compdb` and rejects the xtensa-only flags
  files.".clangd".yaml.CompileFlags.Remove = [
    "-mlongcalls"
    "-mtext-section-literals"
    "-mdisable-hardware-atomics"
    "-fstrict-volatile-bitfields"
    "-fno-tree-switch-conversion"
    "-fno-shrink-wrap"
  ];

  treefmt.config.imports = [./nix/treefmt.nix];

  scripts = let
    # the by-id link keeps working when the Dial comes back as another ttyACM after a reset
    port = ''"''${DIAL_PORT:-$(ls /dev/serial/by-id/usb-Espressif_* | head -n1)}"'';
  in {
    build.exec = ''pio run "$@"'';
    flash.exec = ''pio run -t upload --upload-port ${port} "$@"'';
    # secrets.ini, into the partition that `flash` leaves alone
    provision.exec = ''python3 "$DEVENV_ROOT/tools/flash.py" --port ${port} --secrets "$DEVENV_ROOT/secrets.ini" "$@"'';
    monitor.exec = ''pio device monitor -p ${port} "$@"'';
    # compile_commands.json for clangd
    compdb.exec = ''pio run -t compiledb "$@"'';
    fonts.exec = ''bash "$DEVENV_ROOT/tools/fonts.sh"'';
    dial.exec = ''python3 "$DEVENV_ROOT/tools/dial.py" "$@"'';
  };

  enterTest = "pio run";
}
