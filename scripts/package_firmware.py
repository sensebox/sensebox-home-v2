#!/usr/bin/env python3
# Packages a device build for GitHub Releases (used by .github/workflows/ci.yml, runs after pio run):
#   <out>/release/sensebox-home-v2_<version>_<env>.bin + SHA256SUMS.txt
# The version is read from the OTA image marker, so the file name always matches what the box
# reports. Also prints a Markdown size summary for the CI job summary.
#
# Usage: scripts/package_firmware.py <env> <out dir>
import csv
import hashlib
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MARKER = re.compile(rb"@@SBH2-IMAGE@@ fw=([^ ;\x00]+) hw=[^ ;\x00]+;")


def app_slot_size():
    """Size of the first OTA app slot in partitions.csv."""
    with open(ROOT / "partitions.csv") as f:
        rows = [[c.strip() for c in r] for r in csv.reader(l for l in f if not l.startswith("#"))]
    for name, type_, subtype, offset, size, *_ in (r for r in rows if len(r) >= 5):
        if type_ == "app" and subtype in ("ota_0", "factory"):
            return int(size, 0)
    raise SystemExit("package_firmware: no app partition in partitions.csv")


def main(env, out):
    app = ROOT / ".pio" / "build" / env / "firmware.bin"
    image = app.read_bytes()
    found = MARKER.search(image)
    if not found:
        raise SystemExit("package_firmware: %s has no OTA image marker" % app)
    version = found.group(1).decode()

    release = out / "release"
    release.mkdir(parents=True, exist_ok=True)
    name = "sensebox-home-v2_%s_%s.bin" % (version, env)
    shutil.copy(app, release / name)
    (release / "SHA256SUMS.txt").write_text("%s  %s\n" % (hashlib.sha256(image).hexdigest(), name))

    slot = app_slot_size()
    print("### Firmware %s (`%s`)\n" % (version, env))
    print("`%s`: %d bytes, %.1f %% of the %d KB app slot"
          % (name, len(image), 100.0 * len(image) / slot, slot // 1024))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: %s <env> <out dir>" % sys.argv[0])
    main(sys.argv[1], Path(sys.argv[2]).resolve())
