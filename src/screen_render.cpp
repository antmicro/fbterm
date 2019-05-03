/*
 *   Copyright © 2008 dragchan <zgchan317@gmail.com>
 *   This file is part of FbTerm.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2
 *   of the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define fb_readb(addr) (*(volatile u8 *)(addr))
#define fb_readw(addr) (*(volatile u16 *)(addr))
#define fb_readl(addr) (*(volatile u32 *)(addr))
#define fb_writeb(addr, val) (*(volatile u8 *)(addr) = (val))
#define fb_writew(addr, val) (*(volatile u16 *)(addr) = (val))
#define fb_writel(addr, val) (*(volatile u32 *)(addr) = (val))

typedef void (*drawFun)(u8 *dst, u8 *pixmap, u32 width, u8 fc, u8 bc);
static drawFun draw;

static u32 ppl, ppw, ppb;
static u32 fillColors[NR_COLORS];
static const Color *palette = 0;

void Screen::setPalette(const Color *_palette)
{
	if (palette == _palette) return;
	palette = _palette;

	for (u32 i = NR_COLORS; i--;) {
		if (vinfo.bits_per_pixel == 8) {
			fillColors[i] = (i << 24) | (i << 16) | (i << 8) | i;
		} else {
			fillColors[i] = (palette[i].red >> (8 - vinfo.red.length) << vinfo.red.offset)
					| ((palette[i].green >> (8 - vinfo.green.length)) << vinfo.green.offset)
					| ((palette[i].blue >> (8 - vinfo.blue.length)) << vinfo.blue.offset);

			if (vinfo.bits_per_pixel == 16 || vinfo.bits_per_pixel == 15) {
				fillColors[i] |= fillColors[i] << 16;
			}
		}
	}

	setupSysPalette(false);
	eraseMargin(true, mRows);
}

void Screen::setupSysPalette(bool restore)
{
	if (finfo.visual == FB_VISUAL_TRUECOLOR) return;

	static bool palette_saved = false;
	static u16 saved_red[256], saved_green[256], saved_blue[256];
	u32 cols, rcols, gcols, bcols;
	fb_cmap cmap;

	#define INIT_CMAP(_red, _green, _blue) \
	do { \
		cmap.start = 0; \
		cmap.len = cols; \
		cmap.red = _red; \
		cmap.green = _green; \
		cmap.blue = _blue; \
		cmap.transp = 0; \
	} while (0)

	if (finfo.visual == FB_VISUAL_PSEUDOCOLOR) cols = NR_COLORS;
	else {
		rcols = 1 << vinfo.red.length;
		gcols = 1 << vinfo.green.length;
		bcols = 1 << vinfo.blue.length;

		cols = MAX(rcols, MAX(gcols, bcols));
	}

	if (restore) {
		if (!palette_saved) return;

		INIT_CMAP(saved_red, saved_green, saved_blue);
		ioctl(mFd, FBIOPUTCMAP, &cmap);
	} else {
		if (!palette_saved) {
			palette_saved = true;

			INIT_CMAP(saved_red, saved_green, saved_blue);
			ioctl(mFd, FBIOGETCMAP, &cmap);
		}

		u16 red[cols], green[cols], blue[cols];

		if (finfo.visual == FB_VISUAL_PSEUDOCOLOR) {
			for (u32 i = 0; i < NR_COLORS; i++) {
				red[i] = (palette[i].red << 8) | palette[i].red;
				green[i] = (palette[i].green << 8) | palette[i].green;
				blue[i] = (palette[i].blue << 8) | palette[i].blue;
			}
		} else {
			for (u32 i = 0; i < rcols; i++) {
				red[i] = (65535 / (rcols - 1)) * i;
			}

			for (u32 i = 0; i < gcols; i++) {
				green[i] = (65535 / (gcols - 1)) * i;
			}

			for (u32 i = 0; i < bcols; i++) {
				blue[i] = (65535 / (bcols - 1)) * i;
			}
		}

		INIT_CMAP(red, green, blue);
		ioctl(mFd, FBIOPUTCMAP, &cmap);
	}
}

static inline void fill(u8* dst, u32 w, u8 color)
{
	u32 c = fillColors[color];

	// get better performance if write-combining not enabled for video memory
	for (u32 i = w / ppl; i--; dst += 4) {
		fb_writel(dst, c);
	}

	if (w & ppw) {
		fb_writew(dst, c);
		dst += 2;
	}

	if (w & ppb) {
		fb_writeb(dst, c);
	}
}

static void drawFtFontBpp8(u8 *dst, u8 *pixmap, u32 width, u8 fc, u8 bc)
{
	bool isfg;

	for (; width--; pixmap++, dst++) {
		isfg = (*pixmap & 0x80);
		fb_writeb(dst, fillColors[isfg ? fc : bc]);
	}
}

static void drawFtFontBpp15(u8 *dst, u8 *pixmap, u32 width, u8 fc, u8 bc)
{
	u32 color;
	u8 red, green, blue;
	u8 pixel;

	for (; width--; pixmap++, dst += 2) {
		pixel = *pixmap;

		if (pixel) {
			red = palette[bc].red + (((palette[fc].red - palette[bc].red) * pixel) >> 8);
			green = palette[bc].green + (((palette[fc].green - palette[bc].green) * pixel) >> 8);
			blue = palette[bc].blue + (((palette[fc].blue - palette[bc].blue) * pixel) >> 8);

			color = ((red >> 3 << 10) | (green >> 3 << 5) | (blue >> 3));
			fb_writew(dst, color);
		} else {
			fb_writew(dst, fillColors[bc]);
		}
	}
}

static void drawFtFontBpp16(u8 *dst, u8 *pixmap, u32 width, u8 fc, u8 bc)
{
	u32 color;
	u8 red, green, blue;
	u8 pixel;

	for (; width--; pixmap++, dst += 2) {
		pixel = *pixmap;

		if (pixel) {
			red = palette[bc].red + (((palette[fc].red - palette[bc].red) * pixel) >> 8);
			green = palette[bc].green + (((palette[fc].green - palette[bc].green) * pixel) >> 8);
			blue = palette[bc].blue + (((palette[fc].blue - palette[bc].blue) * pixel) >> 8);

			color = ((red >> 3 << 11) | (green >> 2 << 5) | (blue >> 3));
			fb_writew(dst, color);
		} else {
			fb_writew(dst, fillColors[bc]);
		}
	}
}

static void drawFtFontBpp32(u8 *dst, u8 *pixmap, u32 width, u8 fc, u8 bc)
{
	u32 color;
	u8 red, green, blue;
	u8 pixel;

	for (; width--; pixmap++, dst += 4) {
		pixel = *pixmap;

		if (pixel) {
			red = palette[bc].red + (((palette[fc].red - palette[bc].red) * pixel) >> 8);
			green = palette[bc].green + (((palette[fc].green - palette[bc].green) * pixel) >> 8);
			blue = palette[bc].blue + (((palette[fc].blue - palette[bc].blue) * pixel) >> 8);

			color = ((red << 16) | (green << 8) | blue);
			fb_writel(dst, color);
		} else {
			fb_writel(dst, fillColors[bc]);
		}
	}
}

static void initFillDraw()
{
	ppl = 4 / bytes_per_pixel;
	ppw = ppl >> 1;
	ppb = ppl >> 2;

	switch (vinfo.bits_per_pixel) {
	case 8:
		draw = drawFtFontBpp8;
		break;
	case 15:
		draw = drawFtFontBpp15;
		break;
	case 16:
		draw = drawFtFontBpp16;
		break;
	case 32:
		draw = drawFtFontBpp32;
		break;
	}
}
