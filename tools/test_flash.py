"""Tests for flash.py, reading the partition images it writes back with ESP-IDF's nvs_tool."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
# components/nvs_flash/nvs_partition_tool/nvs_tool.py in ESP-IDF
NVS_TOOL = os.environ["NVS_TOOL"]

# from before the build picked the backend, which flashing either firmware still takes
SECRETS = """\
[secrets]
backend = tapo
wifi_ssid = Test Net
wifi_password = p@ss;w#rd = "quoted"
ha_host = 192.168.1.2
ha_token = eyJhbGciOiJIUzI1NiJ9.e30.c2lnbmF0dXJl
tapo_hosts = 192.168.1.10, 192.168.1.11
tapo_username = someone@example.com
tapo_password = hunter2
static_ip =
"""
WIFI = {"wifi_ssid": "Test Net", "wifi_password": 'p@ss;w#rd = "quoted"'}
HA = WIFI | {"ha_host": "192.168.1.2", "ha_token": "eyJhbGciOiJIUzI1NiJ9.e30.c2lnbmF0dXJl"}


class FlashTest(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.dir = Path(scratch.name)

    def run_flash(self, secrets, *args, **kwargs):
        path = self.dir / "secrets.ini"
        if secrets is not None:
            path.write_text(secrets)
        return subprocess.run(
            [sys.executable, TOOLS / "flash.py", "--secrets", path, *args],
            capture_output=True,
            text=True,
            check=False,
            **kwargs,
        )

    def decode(self, image):
        self.assertEqual(image.stat().st_size, 0x3000)
        dump = subprocess.run(
            [sys.executable, NVS_TOOL, "--format", "json", "--dump", "minimal", image],
            capture_output=True,
            text=True,
            check=True,
        )
        entries = json.loads(dump.stdout)
        self.assertEqual({entry["namespace"] for entry in entries}, {"secrets"})
        return {entry["key"]: entry["data"] for entry in entries}

    def image(self, secrets, backend):
        image = self.dir / "secrets.bin"
        result = self.run_flash(secrets, "--backend", backend, "--image", image)
        self.assertEqual(result.returncode, 0, result.stderr)
        return self.decode(image)

    def test_ha(self):
        self.assertEqual(self.image(SECRETS, "ha"), HA)

    def test_tapo(self):
        self.assertEqual(
            self.image(SECRETS, "tapo"),
            WIFI
            | {
                "tapo_hosts": "192.168.1.10, 192.168.1.11",
                # sha256(sha1(username) + sha1(password)), which the bulbs check logins against
                "tapo_auth_hash": "e33336695a414ace741d6c9ec286a56d19c78ae89f495e5c2fac21ffc20f6f3a",
            },
        )

    def test_static_ip(self):
        address = {"static_ip": "192.168.1.50", "gateway": "192.168.1.1", "subnet": "255.255.255.0"}
        secrets = SECRETS.replace("static_ip =", "\n".join(f"{key} = {value}" for key, value in address.items()))
        self.assertEqual(self.image(secrets, "ha"), HA | address)

    def test_rejects(self):
        cases = {
            "is missing": (None, "ha"),
            "File contains no section headers": ("wifi_ssid = Test Net", "ha"),
            "has no [secrets] section": ("[wifi]\nwifi_ssid = Test Net", "ha"),
            "option 'wifi_ssid' in section 'secrets' already exists": (
                "[secrets]\nwifi_ssid = Test Net\nwifi_ssid = Test Net",
                "ha",
            ),
            "unknown keys wifi_ssdi": ("[secrets]\nwifi_ssdi = Test Net", "ha"),
            "ha_token must be set": ("[secrets]\nwifi_ssid = x\nha_host = h", "ha"),
            "tapo_username, tapo_password must be set": ("[secrets]\nwifi_ssid = x\ntapo_hosts = h", "tapo"),
            "gateway, subnet must be set": ("[secrets]\nwifi_ssid = x\nha_host = h\nha_token = t\nstatic_ip = s", "ha"),
            # 16 characters, but 32 bytes
            "wifi_ssid is longer than 31 bytes": (
                f"[secrets]\nwifi_ssid = {'é' * 16}\nha_host = h\nha_token = t",
                "ha",
            ),
        }
        for message, (secrets, backend) in cases.items():
            with self.subTest(message):
                result = self.run_flash(secrets, "--backend", backend, "--image", self.dir / "secrets.bin")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertNotIn("Traceback", result.stderr)

    def test_flash(self):
        # a build directory as the flake installs it
        build = self.dir / "build"
        build.mkdir()
        shutil.copy(TOOLS.parent / "partitions.csv", build)
        (build / "flasher_args.json").write_text(
            json.dumps(
                {
                    "write_flash_args": ["--flash_mode", "dio", "--flash_size", "8MB", "--flash_freq", "80m"],
                    "flash_files": {"0x0": "bootloader/bootloader.bin", "0x10000": "ha-dial.bin"},
                }
            )
        )
        # records its arguments, and keeps the image that flash.py deletes afterwards
        esptool = self.dir / "bin" / "esptool"
        esptool.parent.mkdir()
        esptool.write_text(
            f"#!{sys.executable}\n"
            "import json, shutil, sys\n"
            f"json.dump(sys.argv[1:], open({str(self.dir / 'args.json')!r}, 'w'))\n"
            f"shutil.copy(sys.argv[-1], {str(self.dir / 'flashed.bin')!r})\n"
        )
        esptool.chmod(0o755)
        env = os.environ | {"PATH": f"{esptool.parent}{os.pathsep}{os.environ['PATH']}"}

        result = self.run_flash(SECRETS, "--backend", "ha", "--firmware", build, "--port", "/dev/ttyACM9", env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        args = json.loads((self.dir / "args.json").read_text())
        self.assertEqual(
            args[:-1],
            [
                *("--chip", "esp32s3", "--port", "/dev/ttyACM9", "--after", "watchdog-reset", "write-flash"),
                *("--flash-mode", "dio", "--flash-size", "8MB", "--flash-freq", "80m"),
                *("0x0", str(build / "bootloader/bootloader.bin"), "0x10000", str(build / "ha-dial.bin")),
                "0x310000",
            ],
        )
        self.assertEqual(self.decode(self.dir / "flashed.bin"), HA)


if __name__ == "__main__":
    unittest.main()
