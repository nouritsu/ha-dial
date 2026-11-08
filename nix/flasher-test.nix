# tools/test_flash.py, which reads the flasher's images back with ESP-IDF's
# nvs_tool
{
  lib,
  runCommand,
  python,
  esp-idf,
}:
runCommand "ha-dial-flasher-test" {
  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../partitions.csv
      ../tools/flash.py
      ../tools/test_flash.py
    ];
  };
  nativeBuildInputs = [python];
  # the ESP-IDF source alone, without its toolchain
  NVS_TOOL = "${esp-idf.src}/components/nvs_flash/nvs_partition_tool/nvs_tool.py";
} ''
  PYTHONDONTWRITEBYTECODE=1 python $src/tools/test_flash.py -v
  touch $out
''
