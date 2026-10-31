#!/usr/bin/env python3
"""Add A21s OTG diagnostics and prioritize USB storage in initramfs."""
import sys
from pathlib import Path


init = Path(sys.argv[1])
text = init.read_text()

startup_anchor = (
    "echo '[A21s] Linux initramfs started; scanning eMMC, SD and USB storage' "
    ">/dev/console\n"
)
startup_diagnostic = (
    "/bin/busybox sleep 1\n"
    "/bin/a21s-otg-diagnostic >/dev/console 2>&1 || true\n"
)
if "/bin/a21s-otg-diagnostic" not in text:
    if text.count(startup_anchor) != 1:
        raise SystemExit("Could not uniquely find the initramfs startup marker")
    text = text.replace(startup_anchor, startup_anchor + startup_diagnostic, 1)

old_first = "for dev in /dev/mmcblk[0-9] /dev/mmcblk[0-9]p*"
new_first = "for dev in /dev/sd[a-z] /dev/sd[a-z][0-9]*"
old_second = "/dev/sd[a-z] /dev/sd[a-z][0-9]*; do"
new_second = "/dev/mmcblk[0-9] /dev/mmcblk[0-9]p*; do"
if old_first in text:
    if text.count(old_first) != 1 or text.count(old_second) != 1:
        raise SystemExit("Could not uniquely find the old storage scan order")
    text = text.replace(old_first, new_first, 1)
    text = text.replace(old_second, new_second, 1)
elif new_first not in text:
    raise SystemExit("Could not set USB-first storage scan order")

shell_anchor = (
    "echo '[A21s] Emergency shell: inspect /dev, /sys/class/block and mount manually.' "
    ">/dev/console\n"
)
shell_diagnostics = """\
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
print("Initramfs now checks USB first and reports USB/input devices")
