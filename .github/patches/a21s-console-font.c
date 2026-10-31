/* Double the A21s text-console font without rebuilding the kernel. */
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_GLYPHS 512
#define SOURCE_HEIGHT 16
#define TARGET_WIDTH 16
#define TARGET_HEIGHT 24
#define TARGET_BYTES_PER_GLYPH (TARGET_WIDTH * TARGET_HEIGHT / 8)

static unsigned char source[MAX_GLYPHS * SOURCE_HEIGHT];
static unsigned char target[MAX_GLYPHS * TARGET_BYTES_PER_GLYPH];

static int set_large_font(int fd)
{
	struct consolefontdesc old_font = {
		.charcount = MAX_GLYPHS,
		.charheight = SOURCE_HEIGHT,
		.chardata = (char *)source,
	};
	struct console_font_op new_font;
	unsigned int glyph, row, column;
	size_t source_glyph_bytes;
	int ret;

	ret = ioctl(fd, GIO_FONTX, &old_font);
	if (ret < 0)
		return -errno;
	if (old_font.charcount == 0 || old_font.charcount > MAX_GLYPHS ||
	    old_font.charheight != 16)
		return -EINVAL;

	source_glyph_bytes = old_font.charheight;
	memset(target, 0, sizeof(target));
	for (glyph = 0; glyph < old_font.charcount; glyph++) {
		const unsigned char *src = source + glyph * source_glyph_bytes;
		unsigned char *dst = target + glyph * TARGET_BYTES_PER_GLYPH;

		for (row = 0; row < TARGET_HEIGHT; row++) {
			unsigned short expanded = 0;
			unsigned int source_row = row * old_font.charheight / TARGET_HEIGHT;

			for (column = 0; column < TARGET_WIDTH; column++) {
				unsigned int source_column = column * 8U / TARGET_WIDTH;

				if (src[source_row] & (1U << (7U - source_column)))
					expanded |= (unsigned short)(1U << (15U - column));
			}

			dst[row * 2U] = (unsigned char)(expanded >> 8);
			dst[row * 2U + 1U] = (unsigned char)expanded;
		}
	}

	new_font.op = KD_FONT_OP_SET;
	new_font.flags = 0;
	new_font.width = TARGET_WIDTH;
	new_font.height = TARGET_HEIGHT;
	new_font.charcount = old_font.charcount;
	new_font.data = target;
	ret = ioctl(fd, KDFONTOP, &new_font);
	return ret < 0 ? -errno : 0;
}

int main(void)
{
	static const char *const consoles[] = { "/dev/tty0", "/dev/console" };
	size_t i;
	int fd, ret = -ENODEV;

	for (i = 0; i < sizeof(consoles) / sizeof(consoles[0]); i++) {
		fd = open(consoles[i], O_RDWR | O_CLOEXEC | O_NOCTTY);
		if (fd < 0)
			continue;
		ret = set_large_font(fd);
		close(fd);
		if (ret == 0) {
			puts("[A21s] Console font enlarged to 16x24.");
			return 0;
		}
	}

	printf("[A21s] Could not enlarge console font: %s\n", strerror(-ret));
	return 0;
}
