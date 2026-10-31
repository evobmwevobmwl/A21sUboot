/* Read-only A21s S2MU106 OTG status probe for the initramfs. */
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int read_reg(int fd, uint16_t address, uint8_t reg, uint8_t *value)
{
	int ret;
	struct i2c_msg messages[2] = {
		{
			.addr = address,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		},
		{
			.addr = address,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = value,
		},
	};
	struct i2c_rdwr_ioctl_data request = {
		.msgs = messages,
		.nmsgs = 2,
	};

	ret = ioctl(fd, I2C_RDWR, &request);
	if (ret == 2)
		return 0;
	return ret < 0 ? -errno : -EIO;
}

int main(void)
{
	int fd = open("/dev/i2c-0", O_RDWR | O_CLOEXEC);
	uint8_t plug_mon2, chg_ctrl0, chg_ctrl3;
	int source_role, ret;

	if (fd < 0) {
		printf("[A21s OTG] read-only I2C0 probe unavailable: %s\n",
		       strerror(errno));
		return 0;
	}

	ret = read_reg(fd, 0x3c, 0xb4, &plug_mon2);
	if (ret) {
		printf("[A21s OTG] cannot read PDIC 0x3c: %s\n", strerror(-ret));
		close(fd);
		return 0;
	}

	source_role = (plug_mon2 & 0x0e) == 0x0a;
	printf("[A21s OTG] PDIC PLUG_MON2=0x%02x; attached source/DFP=%s\n",
	       plug_mon2, source_role ? "yes" : "no");

	ret = read_reg(fd, 0x3d, 0x18, &chg_ctrl0);
	if (!ret)
		ret = read_reg(fd, 0x3d, 0x1b, &chg_ctrl3);
	if (!ret) {
		printf("[A21s OTG] charger CTRL0=0x%02x mode=0x%x%s; CTRL3=0x%02x\n",
		       chg_ctrl0, chg_ctrl0 & 0x0f,
		       (chg_ctrl0 & 0x0f) == 0x06 ? " (boost mode)" : "",
		       chg_ctrl3);
	} else {
		printf("[A21s OTG] cannot read charger 0x3d: %s\n", strerror(-ret));
	}

	close(fd);
	return 0;
}
