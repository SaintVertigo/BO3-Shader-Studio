"""Run the installed Qt QML verifier and check the frontend bridge/resources."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument("--qt-bin", type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
qml = sorted((root / "ui/qml").glob("*.qml"))
bridge = (root / "src/studio_frontend_bridge.h").read_text(encoding="utf-8")
properties = set(re.findall(r"Q_PROPERTY\(\S+\s+(\w+)", bridge))
methods = set(re.findall(r"Q_INVOKABLE\s+\S+\s+(\w+)\(", bridge))
failures = []
for file in qml:
    used = set(re.findall(r"frontend\.(\w+)", file.read_text(encoding="utf-8")))
    for name in sorted(used - properties - methods):
        failures.append(f"{file.name}: unknown frontend member {name}")
qrc = root / "resources/frontend.qrc"
resources = {element.attrib.get("alias") for element in ET.parse(qrc).iter("file")}
for file in qml:
    if file.name not in resources:
        failures.append(f"{file.name}: missing QRC entry")
for element in ET.parse(qrc).iter("file"):
    if not (qrc.parent / element.text).is_file():
        failures.append(f"Missing resource: {element.text}")
with tempfile.TemporaryDirectory() as directory:
    output = Path(directory) / "lint.json"
    result = subprocess.run([str(args.qt_bin / "qmllint.exe"), "--unqualified", "disable",
                             "--json", str(output), *map(str, qml)], check=False)
    report = json.loads(output.read_text(encoding="utf-8"))
    for file in report["files"]:
        for warning in file["warnings"]:
            failures.append(f"{Path(file['filename']).name}:{warning['line']}: {warning['message']}")
    if result.returncode and not failures:
        failures.append(f"qmllint exited {result.returncode}")
for failure in failures:
    print(failure)
print(f"Frontend static audit: {'FAIL' if failures else 'PASS'} ({len(qml)} QML files)")
raise SystemExit(bool(failures))
