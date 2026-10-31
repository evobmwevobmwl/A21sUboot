#!/usr/bin/env python3
"""Add source-gated A21s OTG setup and prioritize USB storage in initramfs."""
import re
import sys
from pathlib import Path


init = Path(sys.argv[1])
text = init.read_text()

setup_anchor = "/bin/busybox mkdir -p /mnt/source /newroot /newroot-bind /run\n"
otg_setup = (
    "/bin/busybox sleep 1\n"
    "tries=0\n"
    "while [ ! -c /dev/i2c-0 ] && [ \"$tries\" -lt 5 ]; do\n"
    "  /bin/busybox sleep 1\n"
    "  tries=$((tries + 1))\n"
    "done\n"
    "/bin/a21s-otg-init >/run/a21s-otg-status 2>&1 || true\n"
    "/bin/busybox cat /run/a21s-otg-status >/dev/console 2>&1 || true\n"
)
if "/bin/a21s-otg-init" not in text:
    if text.count(setup_anchor) != 1:
        raise SystemExit("Could not uniquely find the initramfs device setup marker")
    text = text.replace(setup_anchor, setup_anchor + otg_setup, 1)

internal_first_scan = "for dev in /dev/mmcblk[0-9] /dev/mmcblk[0-9]p* /dev/sd*; do"
scan_patterns = (
    re.compile(
        r"for dev in /dev/mmcblk\[0-9\] /dev/mmcblk\[0-9\]p\* \\\n"
        r"\s*/dev/sd\[a-z\] /dev/sd\[a-z\]\[0-9\]\*; do"
    ),
    re.compile(
        r"for dev in /dev/sd\[a-z\] /dev/sd\[a-z\]\[0-9\]\* \\\n"
        r"\s*/dev/mmcblk\[0-9\] /dev/mmcblk\[0-9\]p\*; do"
    ),
    re.compile(
        r"for dev in /dev/sd\* /dev/mmcblk\[0-9\] /dev/mmcblk\[0-9\]p\*; do"
    ),
)
if internal_first_scan not in text:
    matches = [pattern for pattern in scan_patterns if pattern.search(text)]
    if len(matches) != 1:
        raise SystemExit("Could not uniquely find the storage scan loop")
    text, count = matches[0].subn(internal_first_scan, text, count=1)
    if count != 1:
        raise SystemExit("Could not update the storage scan loop")

archive_anchor = 'echo "[A21s] Found rootfs archive on $dev ($fs)" >/dev/console\n'
archive_message = 'echo "[A21s] Found rootfs archive on $dev ($fs): $archive" >/dev/console\n'
if archive_message not in text:
    if text.count(archive_anchor) != 1:
        raise SystemExit("Could not uniquely find the archive-found message")
    text = text.replace(archive_anchor, archive_message, 1)

shell_anchor = (
    "echo '[A21s] Emergency shell: inspect /dev, /sys/class/block and mount manually.' "
    ">/dev/console\n"
)
shell_diagnostics = """\
echo '[A21s] Android /data may be encrypted; if its archive is inaccessible, copy it to /cache/rootfs.tar.gz or USB.' >/dev/console
echo '[A21s] OTG/VBUS status:' >/dev/console
/bin/busybox cat /run/a21s-otg-status >/dev/console 2>&1 || true
echo '[A21s] USB block devices:' >/dev/console
for block in /sys/class/block/sd*; do
  [ -e "$block" ] || continue
  echo "  $(/bin/busybox basename "$block")" >/dev/console
done
echo '[A21s] Registered input event devices:' >/dev/console
for event in /sys/class/input/event*; do
  [ -e "$event" ] || continue
  name="$(/bin/busybox cat "$event/device/name" 2>/dev/null)"
  echo "  $(/bin/busybox basename "$event"): $name" >/dev/console
done
"""
if "[A21s] Registered input event devices:" not in text:
    if text.count(shell_anchor) != 1:
        raise SystemExit("Could not find the emergency-shell status marker")
    text = text.replace(shell_anchor, shell_anchor + shell_diagnostics, 1)

init.write_text(text)
print("Initramfs enables VBUS only on confirmed source attach, scans internal storage first, and reports USB/input devices")
