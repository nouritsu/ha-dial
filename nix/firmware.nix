# the firmware for one backend, and a flasher that writes it with secrets.ini
{
  lib,
  stdenv,
  cacert,
  esptool,
  python3,
  makeWrapper,
  writeText,
  esp-idf,
  backend,
  version,
}: let
  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../CMakeLists.txt
      ../dependencies.lock
      ../partitions.csv
      ../sdkconfig.defaults
      ../src
    ];
  };

  # PlatformIO adds src as a component the same way
  idf = ''
    export HOME=$TMPDIR
    idf.py -B "$TMPDIR/build" -DIDF_TARGET=esp32s3 -DEXTRA_COMPONENT_DIRS="$PWD/src"'';

  # the esp-idf hooks would otherwise configure and build with cmake and ninja
  noHooks = {
    dontUseCmakeConfigure = true;
    dontUseNinjaBuild = true;
    dontUseNinjaInstall = true;
    dontUseNinjaCheck = true;
  };

  # the registry components in dependencies.lock, fetched ahead of the build,
  # which has no network
  components = stdenv.mkDerivation (noHooks
    // {
      # Nix finds a fixed output by its name and hash alone, so without the lock
      # in the name, a changed lock would quietly reuse the old components
      name = "ha-dial-components-${builtins.substring 0 12 (builtins.hashFile "sha256" ../dependencies.lock)}";
      inherit src;
      nativeBuildInputs = [esp-idf];
      SSL_CERT_FILE = "${cacert}/etc/ssl/certs/ca-bundle.crt";
      NIX_SSL_CERT_FILE = "${cacert}/etc/ssl/certs/ca-bundle.crt";

      buildPhase = ''
        ${idf} reconfigure
        if ! cmp -s dependencies.lock ${src}/dependencies.lock; then
          diff ${src}/dependencies.lock dependencies.lock || true
          echo "dependencies.lock is stale: a PlatformIO build updates it"
          exit 1
        fi
      '';
      installPhase = "cp -r managed_components $out";
      # patched shebangs would tie the hash to a nixpkgs revision
      dontFixup = true;

      outputHashMode = "recursive";
      outputHashAlgo = "sha256";
      outputHash = "sha256-u7Q4f+SYWeLYXU2ah30uJWpO2X//B+3DIRBJdFil6TQ=";
    });

  # maps the store paths that ESP-IDF's file names and asserts would otherwise
  # leave in the image to /IDF
  reproducible = writeText "sdkconfig.nix" ''
    CONFIG_APP_REPRODUCIBLE_BUILD=y
  '';

  python = python3.withPackages (ps: [(ps.callPackage ./nvs-partition-gen.nix {})]);
  share = "$out/share/ha-dial";
in
  stdenv.mkDerivation (noHooks
    // {
      pname = "ha-dial-${backend}";
      inherit version src;
      nativeBuildInputs = [esp-idf makeWrapper];

      buildPhase = ''
        cp -r --no-preserve=mode ${components} managed_components
        ${idf} -DDIAL_BACKEND=${backend} -DPROJECT_VER=${version} \
          -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;${reproducible}" build
      '';

      installPhase = ''
        install -Dm644 -t ${share} partitions.csv sdkconfig
        install -Dm755 ${../tools/flash.py} ${share}/flash.py
        cd "$TMPDIR/build"
        install -Dm644 -t ${share} ha-dial.bin ha-dial.elf flasher_args.json
        install -Dm644 -t ${share}/bootloader bootloader/bootloader.bin
        install -Dm644 -t ${share}/partition_table partition_table/partition-table.bin
        makeWrapper ${python.interpreter} $out/bin/ha-dial-${backend} \
          --add-flags "${share}/flash.py --backend ${backend} --firmware ${share}" \
          --prefix PATH : ${lib.makeBinPath [esptool]}
      '';

      disallowedReferences = [esp-idf esp-idf.tools.xtensa-esp-elf];

      # for the flasher's tests
      passthru = {inherit python;};

      meta = {
        description = "M5Stack Dial firmware for the ${backend} backend, with a flasher for secrets.ini";
        mainProgram = "ha-dial-${backend}";
        platforms = lib.platforms.linux;
      };
    })
