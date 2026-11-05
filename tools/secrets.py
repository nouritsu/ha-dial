import configparser
from pathlib import Path

Import("env")  # noqa: F821

OPTIONAL = {"static_ip", "gateway", "subnet", "dns"}

path = Path(env.subst("$PROJECT_DIR")) / "secrets.ini"  # noqa: F821
if not path.exists():
    raise SystemExit(f"{path} is missing: copy secrets.example.ini and fill it in")

parser = configparser.ConfigParser(interpolation=None)
parser.read(path)
for key, value in parser["secrets"].items():
    if key in OPTIONAL and not value:
        continue
    macro = value if value.isdigit() else env.StringifyMacro(value)  # noqa: F821
    env.Append(CPPDEFINES=[(key.upper(), macro)])  # noqa: F821
