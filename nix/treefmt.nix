# on top of conf-nixos's formatters, for devenv's git hooks and the flake's
# formatting check alike
{
  programs.clang-format.enable = true;
  programs.ruff-check.enable = true;
  programs.ruff-format.enable = true;
  settings.global.excludes = ["src/ui/fonts/*"];
}
