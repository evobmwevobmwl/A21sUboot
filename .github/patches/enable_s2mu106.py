#!/usr/bin/env python3
"""Wire the A21s S2MU106 helper into the upstream E850-96 U-Boot board."""
import shutil
import sys
from pathlib import Path

root = Path(sys.argv[1])
board = root / 'board/samsung/e850-96'

helper = Path(__file__).with_name('a21s_s2mu106.c')
target = board / 'a21s_s2mu106.c'
shutil.copyfile(helper, target)

source = board / 'e850-96.c'
text = source.read_text()
anchor = 'int board_late_init(void)\n'
if text.count(anchor) != 1:
    raise SystemExit('Could not uniquely find board_late_init()')
if 'void a21s_usb_vbus_if_host_attached(void);' in text:
    raise SystemExit('S2MU106 function declaration already exists')
text = text.replace(anchor,
                    'void a21s_usb_vbus_if_host_attached(void);\n\n' + anchor)
call = '\tsetup_serial();\n\tsetup_ethaddr();'
if text.count(call) != 1:
    raise SystemExit('Could not uniquely insert S2MU106 board hook')
text = text.replace(call,
                    '\tsetup_serial();\n'
                    '\ta21s_usb_vbus_if_host_attached();\n'
                    '\tsetup_ethaddr();')
source.write_text(text)

makefile = board / 'Makefile'
text = makefile.read_text()
obj = 'obj-y := e850-96.o fw.o acpm.o pmic.o bootdev.o'
if text.count(obj) != 1 or 'a21s_s2mu106.o' in text:
    raise SystemExit('Unexpected E850-96 board Makefile')
makefile.write_text(text.replace(obj, obj + ' a21s_s2mu106.o'))

dts = root / 'dts/upstream/src/arm64/exynos/exynos850-a217f.dts'
text = dts.read_text()
if '&i2c_0' in text:
    raise SystemExit('A21s DTS already has an I2C0 override')
text += '\n&i2c_0 {\n\tclock-frequency = <100000>;\n\tstatus = "okay";\n};\n'
dts.write_text(text)

print('Added A21s S2MU106 host-gated VBUS init and enabled I2C0')
