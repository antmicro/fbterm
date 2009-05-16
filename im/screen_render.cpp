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

#define fb_readb(addr) (*(volatile unsigned char *)(addr))
#define fb_readw(addr) (*(volatile unsigned short *)(addr))
#define fb_readl(addr) (*(volatile unsigned *)(addr))
#define fb_writeb(addr, val) (*(volatile unsigned char *)(addr) = (val))
#define fb_writew(addr, val) (*(volatile unsigned short *)(addr) = (val))
#define fb_writel(addr, val) (*(volatile unsigned *)(addr) = (val))

typedef void (*drawFun)(char *dst, char *pixmap, unsigned width, unsigned fc, unsigned bc);
static drawFun draw;

static unsigned ppl, ppw, ppb;
static unsigned fillColors[NR_COLORS];
static const Color palette[NR_COLORS] = {
	{0x00, 0x00, 0x00}, /* 0 */
	{0xaa, 0x00, 0x00}, /* 1 */
	{0x00, 0xaa, 0x00}, /* 2 */
	{0xaa, 0x55, 0x00}, /* 3 */
	{0x00, 0x00, 0xaa}, /* 4 */
	{0xaa, 0x00, 0xaa}, /* 5 */
	{0x00, 0xaa, 0xaa}, /* 6 */
	{0xaa, 0xaa, 0xaa}, /* 7 */
	{0x55, 0x55, 0x55}, /* 8 */
	{0xff, 0x55, 0x55}, /* 9 */
	{0x55, 0xff, 0x55}, /* 10 */
	{0xff, 0xff, 0x55}, /* 11 */
	{0x55, 0x55, 0xff}, /* 12 */
	{0xff, 0x55, 0xff}, /* 13 */
	{0x55, 0xff, 0xff}, /* 14 */
	{0xff, 0xff, 0xff}, /* 15 */
};

static inline void fill(char *dst, unsigned w, unsigned color)
{
	unsigned c = fillColors[color];

	// get better performance if write-combining not enabled for video memory
	for (unsigned i = w / ppl; i--; dst += 4) {
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

static void drawFtFontBpp8(char *dst, char *pixmap, unsigned width, unsigned fc, unsigned bc)
{
	bool isfg;

	for (; width--; pixmap++, dst++) {
		isfg = (*pixmap & 0x80);
		fb_writeb(dst, fillColors[isfg ? fc : bc]);
	}
}

static void drawFtFontBpp15(char *dst, char *pixmap, unsigned width, unsigned fc, unsigned bc)
{
	unsigned color;
	unsigned char red, green, blue;
	unsigned char pixel;

	for (; width--; pixmap++, dst += 2) {
		pixel = *pixmap;

		if (!pixel) fb_writew(dst, fillColors[bc]);
		else if (pixel == 0xff) fb_writew(dst, fillColors[fc]);
		else {
			red = palette[bc].red + (((palette[fc].red - palette[bc].red) * pixel) >> 8);
			green = palette[bc].green + (((palette[fc].green - palette[bc].green) * pixel) >> 8);
			blue = palette[bc].blue + (((palette[fc].blue - palette[bc].blue) * pixel) >> 8);

			color = ((red >> 3 << 10) | (green >> 3 << 5) | (blue >> 3));
			fb_writew(dst, color);
		}
	}
}

static void drawFtFontBpp16(char *dst, char *pixmap, unsigned width, unsigned fc, unsigned bc)
{
	unsigned color;
	unsigned char red, green, blue;
	unsigned char pixel;

	for (; width--; pixmap++, dst += 2) {
		pixel = *pixmap;

		if (!pixel) fb_writew(dst, fillColors[bc]);
		else if (pixel == 0xff) fb_writew(dst, fillColors[fc]);
		else {
			red = palette[bc].red + (((palette[fc].red - palette[bc].red) * pixel) >> 8);
			green = palette[bc].green + (((palette[fc].green - palette[bc].green) * pixel) >> 8);
			blue = palette[bc].blue + (((palette[fc].blue - palette[bc].blue) * pixel) >> 8);

			color = ((red >> 3 << 11) | (green >> 2 << 5) | (blue >> 3));
			fb_writew(dst, color);
		}
	}
}

static void drawFtFontBpp32(char *dst, char *pixmap, unsigned width, unsigned fc, unsigned bc)
{
	unsigned color;
	unsigned char red, green, blue;
	unsigned char pixel;

	for (; width--; pixmap++, dst += 4) {
		pixel = *pixmap;

		if (!pixel) fb_writel(dst, fillColors[bc]);
		else if (pixel == 0xff) fb_writel(dst, fillColors[fc]);
		else {
			red = palette[bc].red + (((palette[fc].red - palette[bc].red) * pixel) >> 8);
			green = palette[bc].green + (((palette[fc].green - palette[bc].green) * pixel) >> 8);
			blue = palette[bc].blue + (((palette[fc].blue - palette[bc].blue) * pixel) >> 8);

			color = ((red << 16) | (green << 8) | blue);
			fb_writel(dst, color);
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

	for (unsigned i = NR_COLORS; i--;) {
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
}
