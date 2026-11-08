#!/usr/bin/env python3
"""Write secrets.ini to the Dial's secrets partition, and a firmware build with it."""

import argparse
import configparser
import csv
import glob
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# the longest value of each key: the firmware reads them into buffers of one
# byte more
LIMITS = {
    "wifi_ssid": 31,
    "wifi_password": 63,
    "ha_host": 63,
    "ha_port": 5,
    "ha_token": 255,
    "tapo_hosts": 255,
    "static_ip": 15,
    "gateway": 15,
    "subnet": 15,
    "dns": 15,
}
# the bulbs only ever see a hash of the TP-Link login, so the password itself
# stays on this machine
LOGIN = ("tapo_username", "tapo_password")
NEEDS = {"ha": ("ha_host", "ha_token"), "tapo": ("tapo_hosts",) + LOGIN}
# the keys only one backend reads, which a Dial running another goes without
OWN = {"ha": {"ha_host", "ha_port", "ha_token"}, "tapo": {"tapo_hosts", "tapo_auth_hash"}}
REPO = Path(__file__).resolve().parent.parent


def default_port():
    if os.environ.get("DIAL_PORT"):
        return os.environ["DIAL_PORT"]
    ports = sorted(glob.glob("/dev/serial/by-id/usb-Espressif_*"))
    return ports[0] if ports else "/dev/ttyACM0"


def read_secrets(path, backend):
    if not path.exists():
        sys.exit(f"{path} is missing: copy secrets.example.ini and fill it in")
    parser = configparser.ConfigParser(interpolation=None)
    try:
        parser.read(path)
    except configparser.Error as error:
        sys.exit(f"{path}: {error.message}")
    if not parser.has_section("secrets"):
        sys.exit(f"{path} has no [secrets] section")
    secrets = {key: value for key, value in parser["secrets"].items() if value}
    # older files still pick the backend, which the build does now
    secrets.pop("backend", None)
    unknown = sorted(secrets.keys() - LIMITS.keys() - set(LOGIN))
    if unknown:
        sys.exit(f"{path}: unknown keys {', '.join(unknown)}")
    needed = ["wifi_ssid", *NEEDS.get(backend, ())]
    if "static_ip" in secrets:
        needed += ["gateway", "subnet"]
    missing = [key for key in needed if key not in secrets]
    if missing:
        sys.exit(f"{path}: {', '.join(missing)} must be set")
    for key, limit in LIMITS.items():
        if len(secrets.get(key, "").encode()) > limit:
            sys.exit(f"{path}: {key} is longer than {limit} bytes")

    username, password = (secrets.pop(key, "") for key in LOGIN)
    if username and password:
        secrets["tapo_auth_hash"] = hashlib.sha256(
            hashlib.sha1(username.encode()).digest() + hashlib.sha1(password.encode()).digest()
        ).hexdigest()
    for other, keys in OWN.items():
        if backend and other != backend:
            for key in keys:
                secrets.pop(key, None)
    return secrets


def partition(table, name):
    with open(table) as f:
        for row in csv.reader(line for line in f if not line.lstrip().startswith("#")):
            cells = [cell.strip() for cell in row]
            if cells and cells[0] == name:
                return int(cells[3], 0), int(cells[4], 0)
    sys.exit(f"{table} has no {name} partition")


def write_image(secrets, size, path, scratch):
    listing = scratch / "secrets.csv"
    with open(listing, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["key", "type", "encoding", "value"])
        writer.writerow(["secrets", "namespace", "", ""])
        for key, value in sorted(secrets.items()):
            writer.writerow([key, "data", "string", value])
    result = subprocess.run(
        [sys.executable, "-m", "esp_idf_nvs_partition_gen", "generate", listing, path, hex(size)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode:
        sys.exit(result.stdout + result.stderr)


def firmware_files(build):
    args = json.loads((build / "flasher_args.json").read_text())
    # esptool 5 spells the options ESP-IDF writes with hyphens
    options = [arg.replace("_", "-") if arg.startswith("--") else arg for arg in args["write_flash_args"]]
    files = [item for offset, name in args["flash_files"].items() for item in (offset, str(build / name))]
    return options + files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=default_port())
    parser.add_argument("--secrets", type=Path, default=Path("secrets.ini"))
    parser.add_argument("--backend", choices=sorted(NEEDS), help="check for its keys and leave the other backends' out")
    parser.add_argument("--firmware", type=Path, help="a build directory with flasher_args.json to flash too")
    parser.add_argument("--image", type=Path, help="only write the partition image to this file")
    args = parser.parse_args()

    secrets = read_secrets(args.secrets, args.backend)
    offset, size = partition((args.firmware or REPO) / "partitions.csv", "secrets")
    with tempfile.TemporaryDirectory() as scratch:
        scratch = Path(scratch)
        if args.image:
            write_image(secrets, size, args.image, scratch)
            return
        image = scratch / "secrets.bin"
        write_image(secrets, size, image, scratch)
        flash = firmware_files(args.firmware) if args.firmware else []
        command = ["esptool", "--chip", "esp32s3", "--port", args.port, "--after", "watchdog-reset"]
        sys.exit(subprocess.run([*command, "write-flash", *flash, hex(offset), image], check=False).returncode)


if __name__ == "__main__":
    main()
