"""
dashIO post-build deploy script for PlatformIO.

After a successful build, automatically copies firmware.bin to the SD card's
downloads/ folder. The SD card is identified by the presence of 'bruce.conf'
(or any marker file listed in SD_MARKERS) to avoid writing to the wrong drive.

Assets are NOT copied here — the firmware handles self-extraction on first boot.

Usage:
    Runs automatically via:  extra_scripts = post:scripts/deploy.py
    To skip deploy:          pio run --disable-auto-clean  (SD card simply not found)
"""

import os
import glob
import shutil
import re
Import("env")  # noqa: F821  (PlatformIO injects this)

# Files that identify the dashIO/Bruce SD card
SD_MARKERS = ["bruce.conf", "dashIO"]

# Firmware version — read from config.h if defined, else use "0.1.0"
VERSION = "0.1.0"
VERSION_FILE = os.path.join(env["PROJECT_DIR"], "src", "config.h")  # noqa: F821
if os.path.isfile(VERSION_FILE):
    with open(VERSION_FILE) as f:
        m = re.search(r'#define\s+DASHIO_VERSION\s+"([^"]+)"', f.read())
        if m:
            VERSION = m.group(1)

FIRMWARE_NAME = f"dashIO.{VERSION}.bin"


def find_sd_card():
    """Return the mount point of the dashIO SD card, or None."""
    # Common Linux mount points
    candidates = []
    candidates += glob.glob("/run/media/*/*")
    candidates += glob.glob("/media/*/*")
    candidates += glob.glob("/mnt/*")

    for path in candidates:
        if not os.path.isdir(path):
            continue
        for marker in SD_MARKERS:
            if os.path.exists(os.path.join(path, marker)):
                return path
    return None


def after_build(source, target, env):  # noqa: F811
    firmware_src = os.path.join(
        env["PROJECT_BUILD_DIR"],
        env["PIOENV"],
        "firmware.bin"
    )

    if not os.path.isfile(firmware_src):
        print(f"\n[deploy] firmware.bin not found at {firmware_src}")
        return

    sd = find_sd_card()
    if sd is None:
        print("\n[deploy] SD card not found — skipping auto-deploy.")
        print("[deploy] Mount your SD card and run 'pio run' again to deploy.")
        return

    downloads_dir = os.path.join(sd, "downloads")
    os.makedirs(downloads_dir, exist_ok=True)

    dst = os.path.join(downloads_dir, FIRMWARE_NAME)
    shutil.copy2(firmware_src, dst)

    size_kb = os.path.getsize(dst) / 1024
    print(f"\n[deploy] SD card found at: {sd}")
    print(f"[deploy] Firmware deployed → {dst}  ({size_kb:.1f} KB)")
    print(f"[deploy] Install via M5Burner: {FIRMWARE_NAME}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)  # noqa: F821
