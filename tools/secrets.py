import configparser
import hashlib
from pathlib import Path

Import("env")  # noqa: F821

OPTIONAL = {"static_ip", "gateway", "subnet", "dns"}
# every backend in src/backends is compiled, so the keys of the ones not picked
# need values too
BACKEND_KEYS = {
    "ha": {"ha_host": "", "ha_port": "8123", "ha_token": ""},
    "tapo": {"tapo_hosts": ""},
}
LOGIN = {"tapo_username", "tapo_password"}

project = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
path = project / "secrets.ini"
if not path.exists():
    raise SystemExit(f"{path} is missing: copy secrets.example.ini and fill it in")

parser = configparser.ConfigParser(interpolation=None)
parser.read(path)
secrets = dict(parser["secrets"])
backend = secrets.pop("backend", "") or "ha"
backends = sorted(p.stem for p in (project / "src" / "backends").glob("*.c"))
if backend not in backends:
    raise SystemExit(f"{path}: backend must be one of {', '.join(backends)}, not {backend}")
env.Append(CPPDEFINES=[("DIAL_BACKEND", f"{backend}_backend")])  # noqa: F821
for keys in BACKEND_KEYS.values():
    for key, default in keys.items():
        secrets[key] = secrets.get(key) or default

for key, value in secrets.items():
    if key in LOGIN or (key in OPTIONAL and not value):
        continue
    macro = value if value.isdigit() else env.StringifyMacro(value)  # noqa: F821
    env.Append(CPPDEFINES=[(key.upper(), macro)])  # noqa: F821

# the bulbs only ever see this hash of the TP-Link login, so the password
# itself stays out of the firmware
username, password = (secrets.get(key, "") for key in ("tapo_username", "tapo_password"))
auth_hash = ""
if username and password:
    auth_hash = hashlib.sha256(
        hashlib.sha1(username.encode()).digest() + hashlib.sha1(password.encode()).digest()
    ).hexdigest()
env.Append(CPPDEFINES=[("TAPO_AUTH_HASH", env.StringifyMacro(auth_hash))])  # noqa: F821
