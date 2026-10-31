/* A21s S2MU106 OTG setup, gated on the PDIC reporting attached source/DFP. */
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define PDIC_ADDR       0x3c
#define CHARGER_ADDR    0x3d
#define PLUG_MON2       0xb4
#define CHG_CTRL0       0x18
#define CHG_CTRL3       0x1b
#define CHG_CTRL9       0x21
#define CHG_STATUS2     0x10
#define REG_OTG_PATH    0x30
#define REG_OCP         0x39
#define REG_SYNC        0x3a
#define REG_INPUT_SENSE 0x3b
#define REG_FAULT       0x94

#define MODE_BUCK       0x01
#define MODE_CHG        0x02
#define MODE_OTG        0x04
#define MODE_TX         0x08
#define MODE_MASK       0x0f
#define PDIC_SOURCE     0x0a
#define PDIC_ROLE_MASK  0x0e

struct charger_snapshot {
	uint8_t ctrl0;
	uint8_t ctrl3;
	uint8_t ctrl9;
	uint8_t sense;
	uint8_t fault;
	uint8_t path;
	uint8_t ocp;
	uint8_t sync;
};

static int read_reg(int fd, uint16_t address, uint8_t reg, uint8_t *value)
{
	struct i2c_msg messages[2] = {
		{ .addr = address, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = address, .flags = I2C_M_RD, .len = 1, .buf = value },
	};
	struct i2c_rdwr_ioctl_data request = {
		.msgs = messages,
		.nmsgs = 2,
	};
	int ret = ioctl(fd, I2C_RDWR, &request);

	if (ret == 2)
		return 0;
	return ret < 0 ? -errno : -EIO;
}

static int write_reg(int fd, uint16_t address, uint8_t reg, uint8_t value)
{
	uint8_t bytes[2] = { reg, value };
	struct i2c_msg message = {
		.addr = address,
		.flags = 0,
		.len = sizeof(bytes),
		.buf = bytes,
	};
	struct i2c_rdwr_ioctl_data request = {
		.msgs = &message,
		.nmsgs = 1,
	};
	int ret = ioctl(fd, I2C_RDWR, &request);

	if (ret == 1)
		return 0;
	return ret < 0 ? -errno : -EIO;
}

static int update_reg(int fd, uint16_t address, uint8_t reg,
		      uint8_t value, uint8_t mask)
{
	uint8_t old_value, new_value, check;
	int ret = read_reg(fd, address, reg, &old_value);

	if (ret)
		return ret;
	new_value = (old_value & (uint8_t)~mask) | (value & mask);
	ret = write_reg(fd, address, reg, new_value);
	if (ret)
		return ret;
	ret = read_reg(fd, address, reg, &check);
	if (ret)
		return ret;
	return (check & mask) == (new_value & mask) ? 0 : -EIO;
}

static void delay_ms(long milliseconds)
{
	struct timespec delay = {
		.tv_sec = milliseconds / 1000,
		.tv_nsec = (milliseconds % 1000) * 1000000L,
	};

	while (nanosleep(&delay, &delay) && errno == EINTR)
		;
}

static int read_snapshot(int fd, struct charger_snapshot *s)
{
	struct {
		uint8_t reg;
		uint8_t *value;
	} registers[] = {
		{ CHG_CTRL0, &s->ctrl0 }, { CHG_CTRL3, &s->ctrl3 },
		{ CHG_CTRL9, &s->ctrl9 }, { REG_INPUT_SENSE, &s->sense },
		{ REG_FAULT, &s->fault }, { REG_OTG_PATH, &s->path },
		{ REG_OCP, &s->ocp }, { REG_SYNC, &s->sync },
	};
	size_t i;
	int ret;

	for (i = 0; i < sizeof(registers) / sizeof(registers[0]); i++) {
		ret = read_reg(fd, CHARGER_ADDR, registers[i].reg,
			       registers[i].value);
		if (ret)
			return ret;
	}
	return 0;
}

static void rollback(int fd, const struct charger_snapshot *s)
{
	/* Disable the boost mode first, then restore the saved control bytes. */
	(void)update_reg(fd, CHARGER_ADDR, CHG_CTRL0,
			 s->ctrl0 & MODE_MASK, MODE_MASK);
	(void)write_reg(fd, CHARGER_ADDR, CHG_CTRL3, s->ctrl3);
	(void)write_reg(fd, CHARGER_ADDR, CHG_CTRL9, s->ctrl9);
	(void)write_reg(fd, CHARGER_ADDR, REG_INPUT_SENSE, s->sense);
	(void)write_reg(fd, CHARGER_ADDR, REG_FAULT, s->fault);
	(void)write_reg(fd, CHARGER_ADDR, REG_OTG_PATH, s->path);
	(void)write_reg(fd, CHARGER_ADDR, REG_OCP, s->ocp);
	(void)write_reg(fd, CHARGER_ADDR, REG_SYNC, s->sync);
}

static int apply_vendor_otg_sequence(int fd, const struct charger_snapshot *s)
{
	uint8_t mode = s->ctrl0 & MODE_MASK;
	uint8_t new_mode = mode | MODE_OTG;
	int ret;

	if (mode & MODE_TX) {
		printf("[A21s OTG] charger is in wireless-TX mode; leaving it unchanged\n");
		return -EBUSY;
	}

	/* Match Samsung's S2MU106 charger sequence and 1.2 A OTG OCP limit. */
	if ((new_mode & MODE_BUCK) && (new_mode & MODE_OTG)) {
		new_mode &= (uint8_t)~MODE_BUCK;
		new_mode |= MODE_CHG;
	} else if (new_mode & MODE_OTG) {
		new_mode &= (uint8_t)~MODE_BUCK;
		new_mode |= MODE_CHG;
	}

	ret = update_reg(fd, CHARGER_ADDR, CHG_CTRL3, 0x08, 0x0c);
	if (ret)
		goto fail;
	ret = update_reg(fd, CHARGER_ADDR, CHG_CTRL3, 0x20, 0x30);
	if (ret)
		goto fail;
	ret = update_reg(fd, CHARGER_ADDR, REG_INPUT_SENSE, 0x00, 0x0c);
	if (ret)
		goto fail;
	delay_ms(30);
	ret = update_reg(fd, CHARGER_ADDR, CHG_CTRL9, 0x00, 0x10);
	if (ret)
		goto fail;
	delay_ms(10);

	if ((mode & (MODE_OTG | MODE_BUCK)) == (MODE_OTG | MODE_BUCK)) {
		/* Samsung vendor path: OTG_BUCK -> OTG. */
		ret = update_reg(fd, CHARGER_ADDR, REG_OTG_PATH, 0x0c, 0x0c);
		if (ret)
			goto fail;
		ret = update_reg(fd, CHARGER_ADDR, CHG_CTRL0, new_mode, MODE_MASK);
		if (ret)
			goto fail;
		ret = update_reg(fd, CHARGER_ADDR, REG_OCP, 0x11, 0x33);
		if (ret)
			goto fail;
		ret = update_reg(fd, CHARGER_ADDR, REG_SYNC, 0x01, 0x03);
		if (ret)
			goto fail;
		delay_ms(20);
		ret = update_reg(fd, CHARGER_ADDR, REG_OTG_PATH, 0x04, 0x0c);
	} else {
		if (new_mode & (MODE_CHG | MODE_BUCK)) {
			ret = update_reg(fd, CHARGER_ADDR, REG_SYNC, 0x01, 0x03);
			if (ret)
				goto fail;
		}
		ret = update_reg(fd, CHARGER_ADDR, CHG_CTRL0, new_mode, MODE_MASK);
		if (ret)
			goto fail;
		ret = update_reg(fd, CHARGER_ADDR, REG_OCP, 0x55, 0xff);
	}
	if (ret)
		goto fail;

	delay_ms(20);
	ret = update_reg(fd, CHARGER_ADDR, REG_FAULT, 0x0c, 0x0c);
	if (ret)
		goto fail;
	ret = update_reg(fd, CHARGER_ADDR, CHG_CTRL3, 0x10, 0x30);
	if (ret)
		goto fail;
	ret = update_reg(fd, CHARGER_ADDR, REG_INPUT_SENSE, 0x04, 0x0c);
	if (ret)
		goto fail;
	delay_ms(20);
	return 0;

fail:
	printf("[A21s OTG] vendor sequence failed: %s; restoring previous charger registers\n",
	       strerror(-ret));
	rollback(fd, s);
	return ret;
}

int main(void)
{
	struct charger_snapshot snapshot;
	uint8_t plug_state, control, status;
	int fd, ret;

	fd = open("/dev/i2c-0", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		printf("[A21s OTG] I2C0 unavailable: %s\n", strerror(errno));
		return 0;
	}

	ret = read_reg(fd, PDIC_ADDR, PLUG_MON2, &plug_state);
	if (ret) {
		printf("[A21s OTG] PDIC 0x3c read failed: %s; charger untouched\n",
		       strerror(-ret));
		close(fd);
		return 0;
	}
	printf("[A21s OTG] PDIC PLUG_MON2=0x%02x; source/DFP=%s\n",
	       plug_state,
	       (plug_state & PDIC_ROLE_MASK) == PDIC_SOURCE ? "yes" : "no");
	if ((plug_state & PDIC_ROLE_MASK) != PDIC_SOURCE) {
		printf("[A21s OTG] no confirmed source attach; VBUS remains unchanged\n");
		close(fd);
		return 0;
	}

	ret = read_snapshot(fd, &snapshot);
	if (ret) {
		printf("[A21s OTG] charger read failed: %s; charger untouched\n",
		       strerror(-ret));
		close(fd);
		return 0;
	}
	printf("[A21s OTG] charger CTRL0=0x%02x mode=0x%x CTRL3=0x%02x\n",
	       snapshot.ctrl0, snapshot.ctrl0 & MODE_MASK, snapshot.ctrl3);
	if ((snapshot.ctrl0 & MODE_MASK) == 0x06)
		printf("[A21s OTG] boost mode is set; applying vendor startup sequence\n");

	ret = apply_vendor_otg_sequence(fd, &snapshot);
	if (ret) {
		close(fd);
		return 0;
	}

	ret = read_reg(fd, PDIC_ADDR, PLUG_MON2, &plug_state);
	if (ret || (plug_state & PDIC_ROLE_MASK) != PDIC_SOURCE) {
		printf("[A21s OTG] source attach disappeared during startup; rolling back\n");
		rollback(fd, &snapshot);
		close(fd);
		return 0;
	}
	ret = read_reg(fd, CHARGER_ADDR, CHG_CTRL0, &control);
	if (ret || (control & MODE_MASK) != 0x06) {
		printf("[A21s OTG] boost mode readback failed; rolling back\n");
		rollback(fd, &snapshot);
		close(fd);
		return 0;
	}
	ret = read_reg(fd, CHARGER_ADDR, CHG_STATUS2, &status);
	if (ret)
		status = 0xff;
	printf("[A21s OTG] VBUS boost enabled, OCP=1.2A; CTRL0=0x%02x STATUS2=0x%02x\n",
	       control, status);
	close(fd);
	return 0;
}
