#!/usr/bin/env python3
"""Add A21s USB, console font, touchscreen keyboard, and rootfs boot setup."""
import re
import sys
from pathlib import Path


init = Path(sys.argv[1])
text = init.read_text()

setup_anchor = "/bin/busybox mkdir -p /mnt/source /newroot /newroot-bind /run\n"
otg_setup = (
    "/bin/a21s-console-font >/dev/console 2>&1 || true\n"
    "/bin/busybox sleep 1\n"
    "tries=0\n"
    "while [ ! -c /dev/i2c-0 ] && [ \"$tries\" -lt 5 ]; do\n"
    "  /bin/busybox sleep 1\n"
    "  tries=$((tries + 1))\n"
    "done\n"
    "/bin/a21s-otg-init >/run/a21s-otg-status 2>&1 || true\n"
    "/bin/busybox cat /run/a21s-otg-status >/dev/console 2>&1 || true\n"
    "/bin/busybox sleep 2\n"
    "echo '[A21s] USB devices:' >/dev/console\n"
    "for usb in /sys/bus/usb/devices/*; do\n"
    "  [ -r \"$usb/idVendor\" ] || continue\n"
    "  vendor=\"$(/bin/busybox cat \"$usb/idVendor\" 2>/dev/null)\"\n"
    "  product=\"$(/bin/busybox cat \"$usb/idProduct\" 2>/dev/null)\"\n"
    "  name=\"$(/bin/busybox cat \"$usb/product\" 2>/dev/null)\"\n"
    "  echo \"  $(/bin/busybox basename \"$usb\"): $vendor:$product $name\" >/dev/console\n"
    "done\n"
    "echo '[A21s] Input event devices:' >/dev/console\n"
    "for event in /sys/class/input/event*; do\n"
    "  [ -e \"$event\" ] || continue\n"
    "  name=\"$(/bin/busybox cat \"$event/device/name\" 2>/dev/null)\"\n"
    "  echo \"  $(/bin/busybox basename \"$event\"): $name\" >/dev/console\n"
    "done\n"
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

if "prepare_alpine_console() {" not in text:
    finish_anchor = 'finish_root() {\n  rootdir="$1"\n'
    finish_setup = '''prepare_alpine_console() {
  target="$1"
  /bin/busybox mkdir -p "$target/usr/sbin"
  /bin/busybox cp /bin/a21s-touchkbd "$target/usr/sbin/a21s-touchkbd"
  if [ -f "$target/etc/inittab" ]; then
    if /bin/busybox grep -q '^tty1::respawn:' "$target/etc/inittab"; then
      /bin/busybox sed -i 's#^tty1::respawn:.*#tty1::respawn:/bin/sh -l#' "$target/etc/inittab"
    else
      echo 'tty1::respawn:/bin/sh -l' >> "$target/etc/inittab"
    fi
    if ! /bin/busybox grep -q 'a21s-touchkbd' "$target/etc/inittab"; then
      echo '::respawn:/usr/sbin/a21s-touchkbd' >> "$target/etc/inittab"
    fi
  fi
}

finish_root() {
  rootdir="$1"
  if [ "$rootdir" = /newroot ]; then
    prepare_alpine_console "$rootdir"
    echo '[A21s] Local root shell and touch keyboard enabled on tty1.' >/dev/console
  fi
'''
    if text.count(finish_anchor) != 1:
        raise SystemExit("Could not uniquely find finish_root for console setup")
    text = text.replace(finish_anchor, finish_setup, 1)

switch_root_pattern = re.compile(
    r'(?m)^([ \t]*)/bin/busybox switch_root "\$rootdir" /sbin/init$'
)
if 'exec /bin/busybox switch_root "$rootdir" /sbin/init' not in text:
    if len(switch_root_pattern.findall(text)) != 1:
        raise SystemExit("Could not uniquely find the switch_root invocation")
    text, count = switch_root_pattern.subn(
        r'\1exec /bin/busybox switch_root "$rootdir" /sbin/init', text, count=1
    )
    if count != 1:
        raise SystemExit("Could not update the switch_root invocation")

if "setsid -c /bin/busybox sh -i" not in text:
    old_shell = "exec /bin/busybox sh\n"
    new_shell = (
        "/bin/a21s-touchkbd >/dev/console 2>&1 &\n"
        "while :; do\n"
        "  /bin/busybox sh -c \\\n"
        "    'exec </dev/tty1 >/dev/tty1 2>&1; exec /bin/busybox setsid -c /bin/busybox sh -i'\n"
        "  /bin/busybox sleep 1\n"
        "done\n"
    )
    if text.count(old_shell) < 1:
        raise SystemExit("Could not find the emergency shell to attach its TTY")
    text = text.replace(old_shell, new_shell)

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
print("Initramfs configures the larger console font and touch keyboard, and runs switch_root as PID 1")
