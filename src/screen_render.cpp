/*
 *   Copyright © 2008-2009 dragchan <zgchan317@gmail.com>
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

#include <stdlib.h>

#define fb_readb(addr) (*(volatile u8 *)(addr))
#define fb_readw(addr) (*(volatile u16 *)(addr))
#define fb_readl(addr) (*(volatile u32 *)(addr))
#define fb_writeb(addr, val) (*(volatile u8 *)(addr) = (val))
#define fb_writew(addr, val) (*(volatile u16 *)(addr) = (val))
#define fb_writel(addr, val) (*(volatile u32 *)(addr) = (val))

typedef void (*fillFun)(u32 offset, u32 width, u8 color);
typedef void (*drawFun)(u32 offset, u8 *pixmap, u32 width, u8 fc, u8 bc);

static fillFun fill;
static drawFun draw;

static u32 ppl, ppw, ppb;
static u32 fillColors[NR_COLORS];
static const Color *palette = 0;

static u8 *bgimage_mem;
static u8 bgcolor;

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
	if (finfo.visual == FB_VISUAL_TRUECOLOR || (!restore && !palette)) return;

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
		ioctl(fbdev_fd, FBIOPUTCMAP, &cmap);
	} else {
		if (!palette_saved) {
			palette_saved = true;

			INIT_CMAP(saved_red, saved_green, saved_blue);
			ioctl(fbdev_fd, FBIOGETCMAP, &cmap);
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
		ioctl(fbdev_fd, FBIOPUTCMAP, &cmap);
	}
}

static void fillX(u32 offset, u32 width, u8 color)
{
	u32 c = fillColors[color];
	u8 *dst = fbdev_mem + offset;

	// get better performance if write-combining not enabled for video memory
	for (u32 i = width / ppl; i--; dst += 4) {
		fb_writel(dst, c);
	}

	if (width & ppw) {
		fb_writew(dst, c);
		dst += 2;
	}

	if (width & ppb) {
		fb_writeb(dst, c);
	}
}

static void fillXBg(u32 offset, u32 width, u8 color)
{
	if (color == bgcolor) {
		memcpy(fbdev_mem + offset, bgimage_mem + offset, width * bytes_per_pixel);
	} else {
		fillX(offset, width, color);
	}
}

static void draw8(u32 offset, u8 *pixmap, u32 width, u8 fc, u8 bc)
{
	bool isfg;
	u8 *dst = fbdev_mem + offset;

	for (; width--; pixmap++, dst++) {
		isfg = (*pixmap & 0x80);
		fb_writeb(dst, fillColors[isfg ? fc : bc]);
	}
}

static void draw8Bg(u32 offset, u8 *pixmap, u32 width, u8 fc, u8 bc)
{
	if (bc != bgcolor) {
		draw8(offset, pixmap, width, fc, bc);
		return;
	}

	bool isfg;
	u8 *dst = fbdev_mem + offset;
	u8 *bgimg = bgimage_mem + offset;

	for (; width--; pixmap++, dst++, bgimg++) {
		isfg = (*pixmap & 0x80);
		fb_writeb(dst, isfg ? fillColors[fc] : (*bgimg));
	}
}

#define drawX(bits, lred, lgreen, lblue, type, fbwrite) \
 \
static void draw##bits(u32 offset, u8 *pixmap, u32 width, u8 fc, u8 bc) \
{ \
	u8 red, green, blue; \
	u8 pixel; \
	type color; \
	type *dst = (type *)(fbdev_mem + offset); \
 \
	for (; width--; pixmap++, dst++) { \
		pixel = *pixmap; \
 \
		if (!pixel) fbwrite(dst, fillColors[bc]); \
		else if (pixel == 0xff) fbwrite(dst, fillColors[fc]); \
		else { \
			red = palette[bc].red + (((palette[fc].red - palette[bc].red) * pixel) >> 8); \
			green = palette[bc].green + (((palette[fc].green - palette[bc].green) * pixel) >> 8); \
			blue = palette[bc].blue + (((palette[fc].blue - palette[bc].blue) * pixel) >> 8); \
 \
			color = ((red >> (8 - lred) << (lgreen + lblue)) | (green >> (8 - lgreen) << lblue) | (blue >> (8 - lblue))); \
			fbwrite(dst, color); \
		} \
	} \
}

drawX(15, 5, 5, 5, u16, fb_writew)
drawX(16, 5, 6, 5, u16, fb_writew)
drawX(32, 8, 8, 8, u32, fb_writel)

#define drawXBg(bits, lred, lgreen, lblue, type, fbwrite) \
 \
static void draw##bits##Bg(u32 offset, u8 *pixmap, u32 width, u8 fc, u8 bc) \
{ \
	if (bc != bgcolor) { \
		draw##bits(offset, pixmap, width, fc, bc); \
		return; \
	} \
 \
	u8 red, green, blue; \
	u8 pixel; \
	type color; \
	type *dst = (type *)(fbdev_mem + offset); \
 \
	u8 redbg, greenbg, bluebg; \
	type *bgimg = (type *)(bgimage_mem + offset); \
 \
	for (; width--; pixmap++, dst++, bgimg++) { \
		pixel = *pixmap; \
 \
		if (!pixel) fbwrite(dst, *bgimg); \
		else if (pixel == 0xff) fbwrite(dst, fillColors[fc]); \
		else { \
			color = *bgimg; \
 \
			redbg = ((color >> (lgreen + lblue)) & ((1 << lred) - 1)) << (8 - lred); \
			greenbg = ((color >> lblue) & ((1 << lgreen) - 1)) << (8 - lgreen); \
			bluebg = (color & ((1 << lblue) - 1)) << (8 - lblue); \
 \
			red = redbg + (((palette[fc].red - redbg) * pixel) >> 8); \
			green = greenbg + (((palette[fc].green - greenbg) * pixel) >> 8); \
			blue = bluebg + (((palette[fc].blue - bluebg) * pixel) >> 8); \
 \
			color = ((red >> (8 - lred) << (lgreen + lblue)) | (green >> (8 - lgreen) << lblue) | (blue >> (8 - lblue))); \
			fbwrite(dst, color); \
		} \
	} \
}

drawXBg(15, 5, 5, 5, u16, fb_writew)
drawXBg(16, 5, 6, 5, u16, fb_writew)
drawXBg(32, 8, 8, 8, u32, fb_writel)

static void initFillDraw()
{
	ppl = 4 / bytes_per_pixel;
	ppw = ppl >> 1;
	ppb = ppl >> 2;

	bool bg = false;
	if (getenv("FBTERM_BACKGROUND_IMAGE")) {
		bg = true;

		u32 color = 0;
		Config::instance()->getOption("color-background", color);
		if (color > 7) color = 0;
		bgcolor = color;

		u32 size = finfo.line_length * vinfo.yres;
		bgimage_mem = new u8[size];
		memcpy(bgimage_mem, fbdev_mem, size);

		scroll_type = Redraw;
		if (vinfo.yoffset) {
			vinfo.yoffset = 0;
			ioctl(fbdev_fd, FBIOPAN_DISPLAY, &vinfo);
		}
	}

	fill = bg ? fillXBg : fillX;

	switch (vinfo.bits_per_pixel) {
	case 8:
		draw = bg ? draw8Bg : draw8;
		break;
	case 15:
		draw = bg ? draw15Bg : draw15;
		break;
	case 16:
		draw = bg ? draw16Bg : draw16;
		break;
	case 32:
		draw = bg ? draw32Bg : draw32;
		break;
	}
}

static void endFillDraw()
{
	if (bgimage_mem) delete[] bgimage_mem;
}
