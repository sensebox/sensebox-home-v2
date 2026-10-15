# PlatformIO post-build check (extra_scripts): the firmware image must contain its OTA image
# marker (lib/ota/src/ImageMarker.h), the core's rollback hook must be overridden
# (lib/updater/src/Updater.cpp) and esp_wifi_init() must be wrapped (src/WifiMemory.cpp).
# Without the marker the firmware would refuse updates to itself; without the hook every update
# would be accepted without self-test; without the wrap service mode can run out of memory.
# The first two failed silently once.
import re
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO)

MARKER = re.compile(rb"@@SBH2-IMAGE@@ fw=[^ ;\x00]+ hw=[^ ;\x00]+;")


def check(source, target, env):
    image = str(target[0])
    with open(image, "rb") as f:
        found = MARKER.search(f.read())
    if not found:
        raise SystemExit("check_image: %s has no OTA image marker (see lib/ota/src/ImageMarker.h)" % image)
    print("check_image: marker %s" % found.group(0).decode())

    elf = image[:-4] + ".elf"
    nm = env.subst("$CC").replace("-gcc", "-nm")
    symbols = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    def defined(name):
        line = next((l for l in symbols.splitlines() if l.endswith(" " + name)), "")
        return " T " in line

    if not defined("verifyRollbackLater"):
        raise SystemExit("check_image: verifyRollbackLater() is not overridden: OTA updates would skip the self-test")
    if not defined("__wrap_esp_wifi_init"):
        raise SystemExit("check_image: esp_wifi_init() is not wrapped (src/WifiMemory.cpp): WiFi buffers unbounded")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check)  # noqa: F821
