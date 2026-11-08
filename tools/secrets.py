import configparser
import hashlib
from pathlib import Path

Import("env")  # noqa: F821

OPTIONAL = {"static_ip", "gateway", "subnet", "dns"}
# both backends are always compiled, so the other one's settings need values too
DEFAULTS = {"backend": "ha", "ha_host": "", "ha_port": "8123", "ha_token": "", "tapo_hosts": ""}
LOGIN = {"tapo_username", "tapo_password"}

path = Path(env.subst("$PROJECT_DIR")) / "secrets.ini"  # noqa: F821
if not path.exists():
    raise SystemExit(f"{path} is missing: copy secrets.example.ini and fill it in")

parser = configparser.ConfigParser(interpolation=None)
parser.read(path)
secrets = dict(parser["secrets"])
for key, default in DEFAULTS.items():
    secrets[key] = secrets.get(key) or default
if secrets["backend"] not in {"ha", "tapo"}:
    raise SystemExit(f"{path}: backend must be ha or tapo, not {secrets['backend']}")

for key, value in secrets.items():
    if key == "backend" or key in LOGIN or (key in OPTIONAL and not value):
        continue
    macro = value if value.isdigit() else env.StringifyMacro(value)  # noqa: F821
    env.Append(CPPDEFINES=[(key.upper(), macro)])  # noqa: F821

if secrets["backend"] == "tapo":
    env.Append(CPPDEFINES=["BACKEND_TAPO"])  # noqa: F821

# the bulbs only ever see this hash of the TP-Link login, so the password
# itself stays out of the firmware
username, password = (secrets.get(key, "") for key in ("tapo_username", "tapo_password"))
auth_hash = ""
if username and password:
    auth_hash = hashlib.sha256(
        hashlib.sha1(username.encode()).digest() + hashlib.sha1(password.encode()).digest()
    ).hexdigest()
env.Append(CPPDEFINES=[("TAPO_AUTH_HASH", env.StringifyMacro(auth_hash))])  # noqa: F821
