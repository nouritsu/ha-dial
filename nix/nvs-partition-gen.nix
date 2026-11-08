# ESP-IDF's NVS partition generator, which tools/flash.py runs on secrets.ini;
# later releases need esp-pylib, which nixpkgs doesn't package
{
  buildPythonPackage,
  fetchPypi,
  setuptools,
  cryptography,
}:
buildPythonPackage rec {
  pname = "esp-idf-nvs-partition-gen";
  version = "0.1.9";
  pyproject = true;

  src = fetchPypi {
    pname = "esp_idf_nvs_partition_gen";
    inherit version;
    hash = "sha256-Q6ObVLJDGJ6REJpOmZaMhk8y4g4Ne5wzBSu9JqXxaQ8=";
  };

  build-system = [setuptools];
  dependencies = [cryptography];
  pythonImportsCheck = ["esp_idf_nvs_partition_gen"];
}
