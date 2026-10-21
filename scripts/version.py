# PlatformIO pre-build script (extra_scripts): sets SBH_FW_VERSION (lib/core/src/Version.h).
# Source, first match wins:
#   1. environment variable SBH_FW_VERSION (e.g. SBH_FW_VERSION=0.2.0 pio run -e mcu_s2)
#   2. in CI (env CI set): git describe, tag v0.2.0 -> "0.2.0", later commits -> "0.2.0-3-gabc1234",
#      no tag yet -> "0.1.0-dev-gabc1234"
#   3. otherwise nothing is defined and Version.h keeps "0.1.0-dev". Local builds don't use git:
#      a define that changes with every commit would rebuild the whole framework each time.
# The value ends up in the OTA image marker, which allows no spaces or ';' (scripts/check_image.py).
import os
import re
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO)

DEFAULT = "0.1.0-dev"


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True,
                          cwd=env.subst("$PROJECT_DIR")).stdout.strip()  # noqa: F821


def from_git():
    try:
        return git("describe", "--tags", "--match", "v[0-9]*", "--dirty")[1:]
    except (OSError, subprocess.CalledProcessError):
        pass
    try:
        return "%s-g%s" % (DEFAULT, git("rev-parse", "--short=7", "HEAD"))
    except (OSError, subprocess.CalledProcessError):
        return None


version = os.environ.get("SBH_FW_VERSION") or (from_git() if os.environ.get("CI") else None)
if version:
    version = re.sub(r"[^A-Za-z0-9._+-]", "-", version)
    env.Append(CPPDEFINES=[("SBH_FW_VERSION", env.StringifyMacro(version))])  # noqa: F821
    print("version: SBH_FW_VERSION=%s" % version)
