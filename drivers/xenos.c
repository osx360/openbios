/*
 *	<xenos.c>
 *
 *   Copyright (C) 2026 John Davis
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#include "config.h"
#include "libopenbios/bindings.h"
#include "drivers/drivers.h"
#include "libc/byteorder.h"
#include "libopenbios/video.h"

void xenos_mask_blit(void);
void xenos_invert_rect(void);
void xenos_fill_rect(void);
void xenos_draw_rect(void);

static inline void
xenos_set32(int x, int y, int color)
{
	// Taken from libxenon.
	uint32_t *fb = (uint32_t*)VIDEO_DICT_VALUE(video.mvirt);
	int base = (((y >> 5)*32*VIDEO_DICT_VALUE(video.w) + ((x >> 5)<<10) + (x&3) + ((y&1)<<2) + (((x&31)>>2)<<3) + (((y&31)>>1)<<6)) ^ ((y&8)<<2));

	fb[base] = __cpu_to_le32(color);
}

static inline uint32_t
xenos_get32(int x, int y)
{
	uint32_t *fb = (uint32_t*)VIDEO_DICT_VALUE(video.mvirt);
	int base = (((y >> 5)*32*VIDEO_DICT_VALUE(video.w) + ((x >> 5)<<10) + (x&3) + ((y&1)<<2) + (((x&31)>>2)<<3) + (((y&31)>>1)<<6)) ^ ((y&8)<<2));

	return __le32_to_cpu(fb[base]);
}

/* ( fbaddr maskaddr width height fgcolor bgcolor -- ) */

void
xenos_mask_blit(void)
{
	ucell bgcolor = POP();
	ucell fgcolor = POP();
	ucell height = POP();
	ucell width = POP();
	unsigned char *mask = (unsigned char *)POP();
	unsigned char *fbaddr = (unsigned char *)POP();

	ucell color;
	int x, y, m, b;

	fgcolor = video_get_color(fgcolor);
	bgcolor = video_get_color(bgcolor);

	int yy = (((uint32_t)fbaddr) - VIDEO_DICT_VALUE(video.mvirt)) / VIDEO_DICT_VALUE(video.rb);
	int xx = ((((uint32_t)fbaddr) - VIDEO_DICT_VALUE(video.mvirt)) % VIDEO_DICT_VALUE(video.rb)) / 4;

	for( y = 0; y < height; y++) {
		for( x = 0; x < (width + 1) >> 3; x++ ) {
			for (b = 0; b < 8; b++) {
				m = (1 << (7 - b));

				if (*mask & m) {
					color = fgcolor;
				} else {
					color = bgcolor;
				}

				xenos_set32(xx + x + b, yy + y, color);
			}
			mask++;
		}
	}
}

/* ( x y w h fgcolor bgcolor -- ) */

void
xenos_invert_rect( void )
{
	ucell bgcolor = POP();
	ucell fgcolor = POP();
	int h = POP();
	int w = POP();
	int y = POP();
	int x = POP();

	bgcolor = video_get_color(bgcolor);
	fgcolor = video_get_color(fgcolor);

	if (!VIDEO_DICT_VALUE(video.ih) || x < 0 || y < 0 || w <= 0 || h <= 0 ||
		x + w > VIDEO_DICT_VALUE(video.w) || y + h > VIDEO_DICT_VALUE(video.h))
		return;

	int yy = y;
	for( ; h--; yy++) {
		int xx = x;
		int ww = w;
		while (ww--) {
			uint32_t val = xenos_get32(xx, yy);
			if (val == fgcolor) {
				xenos_set32(xx, yy, bgcolor);
			} else if (val == bgcolor) {
				xenos_set32(xx, yy, fgcolor);
			}

			xx++;
		}
	}
}

/* ( color_ind x y width height -- ) (?) */
void
xenos_fill_rect(void)
{
	int h = POP();
	int w = POP();
	int y = POP();
	int x = POP();
	int col_ind = POP();

	// Hack for BootX to display colors properly.
	unsigned long col;
	if (col_ind == 0xBFBFBF) {
		col = col_ind;
	} else {
		col = video_get_color(col_ind);
	}

	if (!VIDEO_DICT_VALUE(video.ih) || x < 0 || y < 0 || w <= 0 || h <= 0 ||
		x + w > VIDEO_DICT_VALUE(video.w) || y + h > VIDEO_DICT_VALUE(video.h))
		return;

	int yy = y;
	for( ; h--; yy++) {
		int xx = x;
		int ww = w;
		while (ww--) {
			xenos_set32(xx, yy, col);
			xx++;
		}
	}
}

/* ( adr x y width height -- ) (?) */
void
xenos_draw_rect(void)
{
	int h = POP();
	int w = POP();
	int y = POP();
	int x = POP();
	unsigned long *data = (unsigned long*)POP();

	if (!VIDEO_DICT_VALUE(video.ih) || x < 0 || y < 0 || w <= 0 || h <= 0 ||
		x + w > VIDEO_DICT_VALUE(video.w) || y + h > VIDEO_DICT_VALUE(video.h))
		return;

	int yy = y;
	for( ; h--; yy++) {
		int xx = x;
		int ww = w;
		while (ww--) {
			xenos_set32(xx, yy, *data);
			xx++;
			data++;
		}
	}
}

int
ob_xenos_init(const char *path)
{
	setup_video();

	/* Set global variables ready for fb8-install */
	PUSH(pointer2cell(xenos_mask_blit));
	fword("is-noname-cfunc");
	feval("to fb8-blitmask");
	PUSH( pointer2cell(xenos_fill_rect) );
	fword("is-noname-cfunc");
	feval("to fb8-fillrect");
	PUSH( pointer2cell(xenos_invert_rect) );
	fword("is-noname-cfunc");
	feval("to fb8-invertrect");
	bind_func("draw-rectangle", xenos_draw_rect);

	feval("['] xenos-driver-fcode 2 cells + 1 byte-load");

	return 0;
}
