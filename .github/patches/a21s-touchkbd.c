/* Small text-console soft keyboard driven by the A21s touchscreen. */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#define MAX_PATH 128
#define MAX_NAME 128
#define DEFAULT_ROWS 50
#define DEFAULT_COLS 45
#define KEYBOARD_ROWS 14

struct keypress {
	unsigned short code;
	int shifted;
};

static const unsigned short alpha_row0[] = {
	KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
};
static const unsigned short alpha_row1[] = {
	KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
};
static const unsigned short alpha_row2[] = {
	0, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M, KEY_BACKSPACE,
};
static const unsigned short number_row0[] = {
	KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
};
static const struct keypress number_row1[] = {
	{ KEY_2, 1 }, { KEY_3, 1 }, { KEY_4, 1 }, { KEY_5, 1 },
	{ KEY_7, 1 }, { KEY_MINUS, 0 }, { KEY_EQUAL, 1 },
	{ KEY_9, 1 }, { KEY_0, 1 },
};
static const struct keypress number_row2[] = {
	{ 0, 0 }, { KEY_SLASH, 0 }, { KEY_SEMICOLON, 1 },
	{ KEY_SEMICOLON, 0 }, { KEY_COMMA, 0 }, { KEY_DOT, 0 },
	{ KEY_MINUS, 0 }, { KEY_EQUAL, 0 }, { KEY_BACKSPACE, 0 },
};

static int contains_touch_name(const char *name)
{
	char lower[MAX_NAME];
	size_t i;

	for (i = 0; i + 1 < sizeof(lower) && name[i]; i++)
		lower[i] = (char)tolower((unsigned char)name[i]);
	lower[i] = '\0';
	return strstr(lower, "touch") || strstr(lower, "td4150");
}

static int find_touch(char *device, size_t device_size, char *name, size_t name_size)
{
	DIR *dir = opendir("/sys/class/input");
	struct dirent *entry;

	if (!dir)
		return -1;
	while ((entry = readdir(dir)) != NULL) {
		char sysfs[MAX_PATH];
		char event_path[MAX_PATH];
		FILE *file;
		char current_name[MAX_NAME];

		if (strncmp(entry->d_name, "event", 5))
			continue;
		snprintf(sysfs, sizeof(sysfs), "/sys/class/input/%s/device/name", entry->d_name);
		file = fopen(sysfs, "r");
		if (!file)
			continue;
		if (!fgets(current_name, sizeof(current_name), file)) {
			fclose(file);
			continue;
		}
		fclose(file);
		current_name[strcspn(current_name, "\r\n")] = '\0';
		if (!contains_touch_name(current_name))
			continue;
		snprintf(event_path, sizeof(event_path), "/dev/input/%s", entry->d_name);
		strncpy(device, event_path, device_size - 1);
		device[device_size - 1] = '\0';
		strncpy(name, current_name, name_size - 1);
		name[name_size - 1] = '\0';
		closedir(dir);
		return 0;
	}
	closedir(dir);
	return -1;
}

static int get_axis(int fd, unsigned int code, struct input_absinfo *axis)
{
	memset(axis, 0, sizeof(*axis));
	return ioctl(fd, EVIOCGABS(code), axis);
}

static int emit_event(int fd, unsigned short type, unsigned short code, int value)
{
	struct input_event event;

	memset(&event, 0, sizeof(event));
	gettimeofday(&event.time, NULL);
	event.type = type;
	event.code = code;
	event.value = value;
	return write(fd, &event, sizeof(event)) == (ssize_t)sizeof(event) ? 0 : -1;
}

static int create_virtual_keyboard(void)
{
	static const unsigned short keys[] = {
		KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
		KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
		KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
		KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8,
		KEY_9, KEY_LEFTSHIFT, KEY_BACKSPACE, KEY_ENTER, KEY_SPACE,
		KEY_COMMA, KEY_DOT, KEY_MINUS, KEY_EQUAL, KEY_SLASH, KEY_SEMICOLON,
	};
	struct uinput_setup setup;
	size_t i;
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0)
		fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0)
		goto fail;
	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (ioctl(fd, UI_SET_KEYBIT, keys[i]) < 0)
			goto fail;
	}

	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_VIRTUAL;
	setup.id.vendor = 0x04e8;
	setup.id.product = 0xa21f;
	setup.id.version = 1;
	strncpy(setup.name, "A21s Touch Keyboard", sizeof(setup.name) - 1);
	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0)
		goto fail;
	return fd;

fail:
	close(fd);
	return -1;
}

static void draw_line(int tty, int row, const char *text)
{
	(void)dprintf(tty, "\033[s\033[%d;1H\033[7m%s\033[K\033[0m\033[u", row, text);
}

static void draw_keyboard(int tty, int rows, int numeric, int shifted)
{
	int top = rows - KEYBOARD_ROWS;
	const char *r0 = numeric ?
		"[1] [2] [3] [4] [5] [6] [7] [8] [9] [0]" :
		"[q] [w] [e] [r] [t] [y] [u] [i] [o] [p]";
	const char *r1 = numeric ?
		"  [@] [#] [$] [%] [&] [-] [+] [(] [)]" :
		"  [a] [s] [d] [f] [g] [h] [j] [k] [l]";
	const char *r2 = numeric ?
		"[SH] [/] [:] [;] [,] [.] [-] [=] [BS]" :
		"[SH] [z] [x] [c] [v] [b] [n] [m] [BS]";
	const char *r3 = numeric ?
		"[ABC] [,]      [SPACE]      [.] [ENTER]" :
		"[123] [,]      [SPACE]      [.] [ENTER]";

	(void)dprintf(tty, "\033[s\033[1;%dr\033[u", top);
	draw_line(tty, top + 1, "A21s touch keyboard - tap a key below");
	draw_line(tty, top + 2, r0);
	draw_line(tty, top + 3, r0);
	draw_line(tty, top + 4, r0);
	draw_line(tty, top + 5, r1);
	draw_line(tty, top + 6, r1);
	draw_line(tty, top + 7, r1);
	draw_line(tty, top + 8, r2);
	draw_line(tty, top + 9, r2);
	draw_line(tty, top + 10, r2);
	draw_line(tty, top + 11, r3);
	draw_line(tty, top + 12, r3);
	draw_line(tty, top + 13, r3);
	draw_line(tty, top + 14, shifted ? "SHIFT is on; tap [SH] to turn it off" :
		"Touchscreen is typing to the tty1 root shell");
	(void)fsync(tty);
}

static void tap_key(int uinput, int tty, int rows, int *numeric, int *shifted,
		    double x, double y)
{
	int row, column, count;
	unsigned short key = 0;
	int key_shift = 0;
	int top = rows - KEYBOARD_ROWS;
	double first_key_y = (double)(top + 1) / (double)rows;
	double key_row_height = 3.0 / (double)rows;
	char status[96];

	if (y < first_key_y) {
		count = snprintf(status, sizeof(status),
				 "Touch seen x=%.2f y=%.2f; keyboard begins at y=%.2f",
				 x, y, first_key_y);
		if (count > 0)
			draw_line(tty, top + 14, status);
		return;
	}
	row = (int)((y - first_key_y) / key_row_height);
	if (row < 0)
		row = 0;
	if (row > 3)
		row = 3;

	if (row == 0) {
		column = (int)(x * 10.0);
		if (column > 9)
			column = 9;
		key = *numeric ? number_row0[column] : alpha_row0[column];
	} else if (row == 1) {
		column = (int)(((x - 0.05) / 0.90) * 9.0);
		if (column < 0)
			column = 0;
		if (column > 8)
			column = 8;
		if (*numeric) {
			key = number_row1[column].code;
			key_shift = number_row1[column].shifted;
		} else {
			key = alpha_row1[column];
		}
	} else if (row == 2) {
		column = (int)(x * 9.0);
		if (column > 8)
			column = 8;
		if (*numeric) {
			key = number_row2[column].code;
			key_shift = number_row2[column].shifted;
		} else {
			key = alpha_row2[column];
		}
	} else {
		if (x < 0.18) {
			*shifted = 0;
			*numeric = !*numeric;
			draw_keyboard(tty, rows, *numeric, *shifted);
			return;
		} else if (x < 0.32) {
			key = KEY_COMMA;
		} else if (x < 0.68) {
			key = KEY_SPACE;
		} else if (x < 0.82) {
			key = KEY_DOT;
		} else {
			key = KEY_ENTER;
		}
	}

	if (row == 2 && column == 0) {
		*shifted = !*shifted;
		draw_keyboard(tty, rows, *numeric, *shifted);
		return;
	}
	if (!key)
		return;
	key_shift = key_shift || *shifted;
	if (key_shift && (emit_event(uinput, EV_KEY, KEY_LEFTSHIFT, 1) < 0 ||
			   emit_event(uinput, EV_SYN, SYN_REPORT, 0) < 0))
		return;
	if (emit_event(uinput, EV_KEY, key, 1) < 0 ||
	    emit_event(uinput, EV_SYN, SYN_REPORT, 0) < 0 ||
	    emit_event(uinput, EV_KEY, key, 0) < 0 ||
	    emit_event(uinput, EV_SYN, SYN_REPORT, 0) < 0)
		return;
	if (key_shift)
		(void)emit_event(uinput, EV_KEY, KEY_LEFTSHIFT, 0);
	if (key_shift)
		(void)emit_event(uinput, EV_SYN, SYN_REPORT, 0);

	count = snprintf(status, sizeof(status), "Touch sent keycode %u at x=%.2f y=%.2f",
			 key, x, y);
	if (count > 0) {
		draw_keyboard(tty, rows, *numeric, *shifted);
		draw_line(tty, top + 14, status);
	}
}

int main(int argc, char **argv)
{
	char touch_path[MAX_PATH], touch_name[MAX_NAME];
	struct input_absinfo x_axis, y_axis;
	struct input_event event;
	struct winsize size;
	int x_code = ABS_MT_POSITION_X, y_code = ABS_MT_POSITION_Y;
	int touch_fd, keyboard_fd, tty_fd, rows = DEFAULT_ROWS, cols = DEFAULT_COLS;
	int down = 0, was_down = 0, have_x = 0, have_y = 0;
	int x = 0, y = 0, numeric = 0, shifted = 0;
	ssize_t bytes;

	(void)argc;
	(void)argv;
	if (find_touch(touch_path, sizeof(touch_path), touch_name,
		       sizeof(touch_name)) < 0) {
		puts("[A21s Touch] TD4150 input device not found; retrying later.");
		sleep(20);
		return 1;
	}
	touch_fd = open(touch_path, O_RDONLY | O_CLOEXEC);
	if (touch_fd < 0) {
		printf("[A21s Touch] Cannot open %s: %s\n", touch_path, strerror(errno));
		sleep(20);
		return 1;
	}
	if (get_axis(touch_fd, x_code, &x_axis) < 0) {
		x_code = ABS_X;
		if (get_axis(touch_fd, x_code, &x_axis) < 0)
			goto fail_axis;
	}
	if (get_axis(touch_fd, y_code, &y_axis) < 0) {
		y_code = ABS_Y;
		if (get_axis(touch_fd, y_code, &y_axis) < 0)
			goto fail_axis;
	}
	if (x_axis.maximum <= x_axis.minimum || y_axis.maximum <= y_axis.minimum)
		goto fail_axis;

	keyboard_fd = create_virtual_keyboard();
	if (keyboard_fd < 0) {
		printf("[A21s Touch] Cannot create virtual keyboard: %s\n", strerror(errno));
		sleep(20);
		close(touch_fd);
		return 1;
	}
	tty_fd = open("/dev/tty1", O_RDWR | O_CLOEXEC | O_NOCTTY);
	if (tty_fd < 0)
		tty_fd = open("/dev/console", O_WRONLY | O_CLOEXEC | O_NOCTTY);
	if (tty_fd < 0) {
		printf("[A21s Touch] Cannot open console: %s\n", strerror(errno));
		sleep(20);
		close(touch_fd);
		close(keyboard_fd);
		return 1;
	}
	if (ioctl(tty_fd, TIOCGWINSZ, &size) == 0 && size.ws_row >= 16) {
		rows = size.ws_row;
		if (size.ws_col >= 40)
			cols = size.ws_col;
	}
	(void)cols;
	sleep(8);
	draw_keyboard(tty_fd, rows, numeric, shifted);
	(void)dprintf(STDOUT_FILENO,
		      "[A21s Touch] %s; tap the lower on-screen keyboard. Axes X=%d..%d Y=%d..%d\n",
		      touch_name, x_axis.minimum, x_axis.maximum,
		      y_axis.minimum, y_axis.maximum);
	(void)fsync(STDOUT_FILENO);

	while ((bytes = read(touch_fd, &event, sizeof(event))) == sizeof(event)) {
		if (event.type == EV_ABS) {
			if (event.code == x_code) {
				x = event.value;
				have_x = 1;
			} else if (event.code == y_code) {
				y = event.value;
				have_y = 1;
			} else if (event.code == ABS_MT_TRACKING_ID) {
				down = event.value >= 0;
			}
		} else if (event.type == EV_KEY && event.code == BTN_TOUCH) {
			down = event.value != 0;
		} else if (event.type == EV_SYN && event.code == SYN_REPORT) {
			if (down && !was_down && have_x && have_y) {
				double nx = (double)(x - x_axis.minimum) /
					   (double)(x_axis.maximum - x_axis.minimum);
				double ny = (double)(y - y_axis.minimum) /
					   (double)(y_axis.maximum - y_axis.minimum);

				if (nx < 0.0) nx = 0.0;
				if (nx > 1.0) nx = 1.0;
				if (ny < 0.0) ny = 0.0;
				if (ny > 1.0) ny = 1.0;
				tap_key(keyboard_fd, tty_fd, rows, &numeric, &shifted, nx, ny);
				(void)fsync(tty_fd);
			}
			was_down = down;
		}
	}

	(void)ioctl(keyboard_fd, UI_DEV_DESTROY);
	close(tty_fd);
	close(keyboard_fd);
	close(touch_fd);
	return 1;

fail_axis:
	printf("[A21s Touch] Cannot read touchscreen X/Y ranges from %s.\n", touch_path);
	close(touch_fd);
	sleep(20);
	return 1;
}
