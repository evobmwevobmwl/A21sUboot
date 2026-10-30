// SPDX-License-Identifier: GPL-2.0+
/* A21s-specific S2MU106 OTG source detection and VBUS enable. */

#include <clk.h>
#include <dm/uclass.h>
#include <i2c.h>
#include <stdio.h>

#define A21S_S2MU106_PDIC_ADDR 0x3c
#define A21S_S2MU106_CHARGER_ADDR 0x3d
#define A21S_S2MU106_PLUG_MON2 0xb4
#define A21S_S2MU106_CHG_CTRL0 0x18
#define A21S_S2MU106_CHG_CTRL3 0x1b

void a21s_usb_vbus_if_host_attached(void)
{
	struct udevice *i2c_bus, *pdic, *charger;
	struct clk i2c_clk;
	int plug_state, ctrl3, ctrl0, ret;

	ret = uclass_get_device_by_seq(UCLASS_I2C, 0, &i2c_bus);
	if (ret) {
		printf("A21s OTG: I2C0 lookup failed (%d); VBUS stays off\n", ret);
		return;
	}

	ret = clk_get_by_name(i2c_bus, "i2c", &i2c_clk);
	if (ret) {
		printf("A21s OTG: I2C0 clock lookup failed (%d); VBUS stays off\n",
		       ret);
		return;
	}

	ret = clk_enable(&i2c_clk);
	if (ret) {
		printf("A21s OTG: I2C0 clock enable failed (%d); VBUS stays off\n",
		       ret);
		return;
	}
	printf("A21s OTG: I2C0 clock enabled\n");

	ret = i2c_get_chip_for_busnum(0, A21S_S2MU106_PDIC_ADDR, 1, &pdic);
	if (ret) {
		printf("A21s OTG: PDIC I2C probe failed (%d); VBUS stays off\n", ret);
		return;
	}

	plug_state = dm_i2c_reg_read(pdic, A21S_S2MU106_PLUG_MON2);
	if (plug_state < 0) {
		printf("A21s OTG: attach-state read failed (%d); VBUS stays off\n",
		       plug_state);
		return;
	}
	printf("A21s OTG: S2MU106 PLUG_MON2=0x%02x\n", plug_state);

	/* Vendor S2MU106 driver marks attached source/DFP as masked value 0x0a. */
	if ((plug_state & 0x0e) != 0x0a) {
		printf("A21s OTG: no source-role attach; VBUS stays off\n");
		return;
	}

	ret = i2c_get_chip_for_busnum(0, A21S_S2MU106_CHARGER_ADDR, 1, &charger);
	if (ret) {
		printf("A21s OTG: charger I2C probe failed (%d); VBUS stays off\n", ret);
		return;
	}

	ctrl3 = dm_i2c_reg_read(charger, A21S_S2MU106_CHG_CTRL3);
	ctrl0 = dm_i2c_reg_read(charger, A21S_S2MU106_CHG_CTRL0);
	if (ctrl3 < 0 || ctrl0 < 0) {
		printf("A21s OTG: charger register read failed; VBUS stays off\n");
		return;
	}

	/* Match Samsung's charger driver: 1.5 A OCP, then OTG boost mode. */
	ctrl3 = (ctrl3 & ~0x0c) | 0x0c;
	ret = dm_i2c_reg_write(charger, A21S_S2MU106_CHG_CTRL3, ctrl3);
	if (ret) {
		printf("A21s OTG: OCP configuration failed (%d); VBUS stays off\n", ret);
		return;
	}

	ctrl0 = (ctrl0 & ~0x0f) | 0x06;
	ret = dm_i2c_reg_write(charger, A21S_S2MU106_CHG_CTRL0, ctrl0);
	if (ret) {
		printf("A21s OTG: boost enable failed (%d)\n", ret);
		return;
	}

	printf("A21s OTG: source role confirmed; S2MU106 VBUS boost enabled\n");
}
