#!/usr/bin/env python3
"""Apply the A21s USB host, I2C0, and touchscreen supply DT changes."""
import sys
from pathlib import Path


path = Path(sys.argv[1])
text = path.read_text()

compatible = 'compatible = "samsung,a217f", "samsung,exynos850";\n'
aliases = '\n\taliases {\n\t\ti2c0 = &i2c_0;\n\t};\n'
if "i2c0 = &i2c_0;" not in text:
    if text.count(compatible) != 1:
        raise SystemExit("Could not uniquely find the A21s root compatible property")
    text = text.replace(compatible, compatible + aliases, 1)

if 'dr_mode = "otg";' in text:
    if text.count('dr_mode = "otg";') != 1:
        raise SystemExit("Expected one A21s DWC3 dr_mode property")
    text = text.replace('dr_mode = "otg";', 'dr_mode = "host";', 1)
elif 'dr_mode = "host";' not in text:
    raise SystemExit("Could not find the A21s DWC3 dr_mode property")

vdd = '\t\tvdd-supply = <&reg_dummy>;\n'
vddio = '\t\tvddio-supply = <&reg_dummy>;\n'
if vddio not in text:
    if text.count(vdd) != 1:
        raise SystemExit("Could not uniquely find the touchscreen vdd supply")
    text = text.replace(vdd, vdd + vddio, 1)

i2c = '\n&i2c_0 {\n\tclock-frequency = <100000>;\n\tstatus = "okay";\n};\n'
if "&i2c_0 {" not in text:
    text += i2c

if text.count('dr_mode = "host";') != 1:
    raise SystemExit("A21s DWC3 host-mode change did not apply uniquely")
if text.count("i2c0 = &i2c_0;") != 1:
    raise SystemExit("A21s I2C0 alias did not apply uniquely")
if text.count(vddio) != 1:
    raise SystemExit("A21s touchscreen vddio supply did not apply uniquely")

path.write_text(text)
print("Enabled A21s DWC3 host mode and I2C0; added touchscreen vddio dummy supply")
