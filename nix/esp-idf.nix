# ESP-IDF as PlatformIO builds with it: the version in dependencies.lock, and
# the component manager 2.4 that PlatformIO installs, since esp-dev's 2.2 can't
# evaluate the Kconfig conditions in lvgl's manifest
{
  esp-idf-xtensa,
  python3,
}: let
  component-manager = ps: package:
    package.overridePythonAttrs (old: rec {
      version = "2.4.11";
      src = ps.fetchPypi {
        pname = "idf_component_manager";
        inherit version;
        hash = "sha256-gGeiA5R/JMFtP4ltwvpK8nQ0ff9bwLqcrfUAASnwW8s=";
      };
      propagatedBuildInputs = old.propagatedBuildInputs ++ [ps.psutil];
    });

  python =
    python3
    // {
      withPackages = f:
        python3.withPackages (ps:
          map (p:
            if (p.pname or null) == "idf-component-manager"
            then component-manager ps p
            else p) (f ps));
    };
in
  esp-idf-xtensa.override {
    rev = "v5.5.5";
    sha256 = "sha256-/xVsusdmXFMLImfKryoB5ulR09AJefzoIUFmTAb3n/s=";
    toolsToInclude = ["xtensa-esp-elf"];
    python3 = python;
  }
