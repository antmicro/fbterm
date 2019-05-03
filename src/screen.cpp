/*
 *   Copyright © 2008 dragchan <zgchan317@gmail.com>
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

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kd.h>
#include <linux/fb.h>
#include <ft2build.h>
#include FT_GLYPH_H
#include "fbshell.h"
#include "font.h"
#include "screen.h"

#define fb_readb(addr) (*(volatile u8 *) (addr))
#define fb_readw(addr) (*(volatile u16 *) (addr))
#define fb_readl(addr) (*(volatile u32 *) (addr))
#define fb_writeb(addr,val) (*(volatile u8 *) (addr) = (val))
#define fb_writew(addr,val) (*(volatile u16 *) (addr) = (val))
#define fb_writel(addr,val) (*(volatile u32 *) (addr) = (val))

#define font (Font::instance())
#define W(x) ((x) * font->width())
#define H(y) ((y) * font->height())

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

static const s8 show_cursor[] = "\033[?25h";
static const s8 hide_cursor[] = "\033[?25l";
static const s8 disable_blank[] = "\033[9;0]";
static const s8 enable_blank[] = "\033[9;10]";
static const s8 clear_screen[] = "\033[2J\033[H";

static fb_fix_screeninfo finfo;
static fb_var_screeninfo vinfo;

static Color palette[NR_COLORS];
static u32 colors[NR_COLORS];

DEFINE_INSTANCE(Screen)

Screen *Screen::createInstance()
{
	s8 devname[32];
	s32 devFd;
	for (u32 i = 0; i < FB_MAX; i++) {
		snprintf(devname, sizeof(devname), "/dev/fb%d", i);
		devFd = open(devname, O_RDWR);
        if (devFd >= 0) break;
	}

	if (devFd < 0) {
		printf("can't open framebuffer device!\n");
		return 0;
	}

	fcntl(devFd, F_SETFD, fcntl(devFd, F_GETFD) | FD_CLOEXEC);
	ioctl(devFd, FBIOGET_FSCREENINFO, &finfo);
	ioctl(devFd, FBIOGET_VSCREENINFO, &vinfo);

	if (finfo.type != FB_TYPE_PACKED_PIXELS) {
		printf("unsupported framebuffer device!\n");
		return 0;
	}

	if (vinfo.bits_per_pixel == 15) vinfo.bits_per_pixel = 16;

	if (vinfo.bits_per_pixel != 8 && vinfo.bits_per_pixel != 16 && vinfo.bits_per_pixel != 32) {
		printf("only support framebuffer device with 8/15/16/32 color depth!\n");
		return 0;
	}

	if (!font) return 0;
	
	if (vinfo.xres / W(1) == 0 || vinfo.yres / H(1) == 0) {
		printf("font size is too huge!\n");
		return 0;
	}

	return new Screen(devFd);
}

Screen::Screen(s32 fd)
{
	mFd = fd;
	mpMemStart = (s8 *)mmap(0, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, 0);

	mCols = vinfo.xres / W(1);
	mRows = vinfo.yres / H(1);

	bool ypan = (vinfo.yres_virtual > vinfo.yres && finfo.ypanstep && !(H(1) % finfo.ypanstep));
	bool ywrap = (finfo.ywrapstep && !(H(1) % finfo.ywrapstep));
	if (ywrap && !(vinfo.vmode & FB_VMODE_YWRAP)) {
		vinfo.vmode |= FB_VMODE_YWRAP;
		ioctl(mFd, FBIOPUT_VSCREENINFO, &vinfo);
		ywrap = (vinfo.vmode & FB_VMODE_YWRAP);
	}

	mScrollAccel = ((ypan || ywrap) && !ioctl(mFd, FBIOPAN_DISPLAY, &vinfo)) ? (ywrap ? 2 : 1) : 0;

	s32 ret = write(STDIN_FILENO, hide_cursor, sizeof(hide_cursor) - 1);
	ret = write(STDIN_FILENO, disable_blank, sizeof(disable_blank) - 1);	
}

Screen::~Screen()
{
	setupSysPalette(true);
	munmap(mpMemStart, finfo.smem_len);
	close(mFd);

	if (mScrollAccel) {
		ioctl(STDIN_FILENO, KDSETMODE, KD_GRAPHICS);
		ioctl(STDIN_FILENO, KDSETMODE, KD_TEXT);
	}

	s32 ret = write(STDIN_FILENO, show_cursor, sizeof(show_cursor) - 1);
	ret = write(STDIN_FILENO, enable_blank, sizeof(enable_blank) - 1);	
	ret = write(STDIN_FILENO, clear_screen, sizeof(clear_screen) - 1);
	
	Font::uninstance();
}

void Screen::setPalette(const Color *_palette)
{
	memcpy(palette, _palette, sizeof(palette));

	switch (vinfo.bits_per_pixel) {
	case 8:
		for (u32 i = 0; i < NR_COLORS; i++) {
			colors[i] = (i << 24) | (i << 16) | (i << 8) | i;
		}
		break;
		
	case 16:
	case 32:
		for (u32 i = 0; i < NR_COLORS; i++) {
			colors[i] = (palette[i].red >> (8 - vinfo.red.length) << vinfo.red.offset)
						| ((palette[i].green >> (8 - vinfo.green.length)) << vinfo.green.offset)
						| ((palette[i].blue >> (8 - vinfo.blue.length)) << vinfo.blue.offset);
			if (vinfo.bits_per_pixel == 16) {
				colors[i] |= colors[i] << 16;
			}
		}
		break;
	
	default:
		break;
	}

	setupSysPalette(false);
	eraseMargin(true, mRows);
}

void Screen::switchVc(bool enter)
{
	if (enter) {
		ioctl(mFd, FBIOGET_VSCREENINFO, &vinfo);
		ioctl(mFd, FBIOPAN_DISPLAY, &vinfo);
		
		setupSysPalette(false);
		eraseMargin(true, mRows);
	} else {
		setupSysPalette(true);
	}
}

void Screen::setupSysPalette(bool restore)
{
	if (vinfo.bits_per_pixel != 8) return;

	static bool palette_saved = false;
	static u16 saved_red[NR_COLORS], saved_green[NR_COLORS], saved_blue[NR_COLORS];
	fb_cmap cmap;

	#define INIT_CMAP(_red, _green, _blue) \
	do { \
		cmap.start = 0; \
		cmap.len = NR_COLORS; \
		cmap.red = _red; \
		cmap.green = _green; \
		cmap.blue = _blue; \
		cmap.transp = 0; \
	} while (0)

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

		u16 red[NR_COLORS], green[NR_COLORS], blue[NR_COLORS];

		for (u32 i = 0; i < NR_COLORS; i++) {
			red[i] = palette[i].red << 8 | palette[i].red;
			green[i] = palette[i].green << 8 | palette[i].green;
			blue[i] = palette[i].blue << 8 | palette[i].blue;
		}

		INIT_CMAP(red, green, blue);
		ioctl(mFd, FBIOPUTCMAP, &cmap);
	}
}

void Screen::eraseMargin(bool top, u16 h)
{
	if (vinfo.xres % W(1)) {
		fillRect(W(mCols), top ? 0 : H(mRows - h), vinfo.xres % W(1), H(h), 0);
	}

	if (vinfo.yres % H(1)) {
		fillRect(0, H(mRows), vinfo.xres, vinfo.yres % H(1), 0);
	}
}

bool Screen::move(u16 scol, u16 srow, u16 dcol, u16 drow, u16 w, u16 h)
{
	if (mScrollAccel == 0 || scol != dcol) return false;

	u16 top = MIN(srow, drow), bot = MAX(srow, drow) + h;
	u16 left = scol, right = scol + w;

	u32 noaccel_redraw_area = w * (bot - top - 1);
	u32 accel_redraw_area = mCols * mRows - w * h;

	if (noaccel_redraw_area <= accel_redraw_area) return false;

	s32 yoffset = vinfo.yoffset;
	yoffset += H((s32)srow - drow);
	
	bool redraw_all = false;
	if (mScrollAccel == 1) {
		u32 ytotal = vinfo.yres_virtual - vinfo.yres;
		redraw_all = true;

		if (yoffset < 0) yoffset = ytotal;
		else if ((u32)yoffset > ytotal) yoffset = 0;
		else redraw_all = false;
	} else {
		if (yoffset < 0) yoffset += vinfo.yres_virtual;
		else if ((u32)yoffset >= vinfo.yres_virtual) yoffset -= vinfo.yres_virtual;
	}

	vinfo.yoffset = yoffset;
	ioctl(mFd, FBIOPAN_DISPLAY, &vinfo);

	#define redraw(args...) (FbShellManager::instance()->redraw(args))

	if (redraw_all) {
		redraw(0, 0, mCols, mRows);
		eraseMargin(true, mRows);
	} else {
		if (top) redraw(0, 0, mCols, top);
		if (bot < mRows) redraw(0, bot, mCols, mRows - bot);
		if (left > 0) redraw(0, top, left, bot - top - 1);
		if (right < mCols) redraw(right, top, mCols - right, bot - top - 1);
		eraseMargin(drow > srow, drow > srow ? (drow - srow) : (srow - drow));
	}

	return true;
}

void Screen::drawUnderline(u16 col, u16 row, u8 color)
{
	fillRect(W(col), H(row + 1) - 1, W(1),  1, color);
}

void Screen::clear(u16 col, u16 row, u16 w, u16 h, u8 color)
{
	fillRect(W(col), H(row), W(w), H(h), color);
}

void Screen::fillRect(u32 x, u32 y, u32 w, u32 h, u8 color)
{
	if (x >= vinfo.xres || y >= vinfo.yres) return;
	if (x + w > vinfo.xres) w = vinfo.xres - x;
	if (y + h > vinfo.yres) h = vinfo.yres - y;

	u32 fg = colors[color];
	s8 *dst8 = mpMemStart + x * vinfo.bits_per_pixel / 8;

	u32 ppl = 32 / vinfo.bits_per_pixel, ppw = ppl >> 1, ppb = ppl >> 2;

	for(; h--; y++)
	{
		u32 *dst32 = (u32 *)(dst8 + ((vinfo.yoffset + y) % vinfo.yres_virtual) * finfo.line_length);

		for (u32 i = w / ppl; i--;) {
			fb_writel(dst32++, fg);
		}

		if (ppw && (w & ppw)) {
			fb_writew(dst32, fg);
			dst32 = (u32*)((u8*)dst32 + 2);
		}

		if (ppb && (w & ppb)) {
			fb_writeb(dst32, fg);
		}
	}
}

void Screen::drawText(u16 col, u16 row, u8 fc, u8 bc, u16 num, u16 *text, bool *dw)
{
	u32 startx, x = W(col), y = H(row), w = W(1);
	u16 startnum, *starttext;
	bool *startdw, draw_space = false, draw_text = false;

	for (; num; num--, text++, dw++, x += w) {
		if (*text == 0x20) {
			if (draw_text) {
				draw_text = false;
				drawGlyphs(startx, y, fc, bc, startnum - num, starttext, startdw);
			}

			if (!draw_space) {
				draw_space = true;
				startx = x;
			}
		} else {
			if (draw_space) {
				draw_space = false;
				fillRect(startx, y, x - startx, H(1), bc);
			}

			if (!draw_text) {
				draw_text = true;
				starttext = text;
				startdw = dw;
				startnum = num;
				startx = x;
			}

			if (*dw) x += w;
		}
	}

	if (draw_text) {
		drawGlyphs(startx, y, fc, bc, startnum - num, starttext, startdw);
	} else if (draw_space) {
		fillRect(startx, y, x - startx, H(1), bc);
	}
}

void Screen::drawGlyphs(u32 x, u32 y, u8 fc, u8 bc, u16 num, u16 *text, bool *dw)
{
	if (font->isMonospace()) {
		for (; num; num--, text++, dw++) {
			drawGlyph(x, y, fc, bc, *text, *dw, true);
			x += *dw ? W(2) : W(1);
		}
	} else {
		int w = num;
		for (u16 i = 0; i < num; i++) {
			if (dw[i]) w++;
		}
		fillRect(x, y, W(w), H(1), bc);

		for (; num; num--, text++, dw++) {
			x += drawGlyph(x, y, fc, bc, *text, *dw, false);
		}
	}
}

u32 Screen::drawGlyph(u32 x, u32 y, u8 fc, u8 bc, u16 code, bool dw, bool fillbg)
{
	if (x >= vinfo.xres || y >= vinfo.yres) return 0;

	s32 w = (dw ? W(2) : W(1)), h = H(1);
	if (x + w > vinfo.xres) w = vinfo.xres - x;
	if (y + h > vinfo.yres) h = vinfo.yres = y;

	Font::Glyph *fglyph = (Font::Glyph *)font->getGlyph(code);
	if (!fglyph) {
		if (fillbg) fillRect(x, y, w, h, bc);
		return w;
	}
		
	FT_BitmapGlyph glyph = (FT_BitmapGlyph)fglyph->glyph;

	s32 top = fglyph->baseline - glyph->top;
	if (top < 0) top = 0;

	s32 left = glyph->left;
	if ((s32)x + left < 0) left = -x;
	
	s8 *dst, *dst_line = mpMemStart + ((s32)x + left) * vinfo.bits_per_pixel / 8;
	u8 red, green, blue,  *pixmap = glyph->bitmap.buffer;
	u32 color;
	s32 width = glyph->bitmap.width;

	if (fillbg) {
		if (width > w - left) width = w - left;

		if (top) fillRect(x, y, w, top, bc);
		if (left > 0) fillRect(x, y + top, left, glyph->bitmap.rows, bc);
	
		s32 right = width + left;
		if (w > right) fillRect((s32)x + right, y + top, w - right, glyph->bitmap.rows, bc);
	
		s32 bot = top + glyph->bitmap.rows;
		if (h > bot) fillRect(x, y + bot, w, h - bot, bc);
	}

	if ((s32)x + left >= (s32)vinfo.xres || (s32)y + top >= (s32)vinfo.yres) return 0;

	if ((s32)x + left + width > (s32)vinfo.xres) width = vinfo.xres - ((s32)x + left);

	s32 height = glyph->bitmap.rows;
	if (y + top + height > vinfo.yres) height = vinfo.yres - (y + top);

	bool isfg;	
	for (s32 i = 0; i < height; i++, y++, pixmap += glyph->bitmap.pitch) {
		dst = dst_line + ((vinfo.yoffset + y + top) % vinfo.yres_virtual) * finfo.line_length;

		for (s32 j = 0; j < width; j++) {
			if (glyph->bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
				isfg = (pixmap[j >> 3] & (0x80 >> (j & 7)));
				color = colors[isfg ? fc : bc];
			} else {
				if (vinfo.bits_per_pixel == 8) {
					isfg = (pixmap[j] & 0x80);
					color = colors[isfg ? fc : bc];
				} else {
					isfg = pixmap[j];
					red = palette[bc].red + ((palette[fc].red - palette[bc].red) * pixmap[j]) / 255;
					green = palette[bc].green + ((palette[fc].green - palette[bc].green) * pixmap[j]) / 255;
					blue = palette[bc].blue + ((palette[fc].blue - palette[bc].blue) * pixmap[j]) / 255;

					color = (red >> (8 - vinfo.red.length) << vinfo.red.offset)
							| (green >> (8 - vinfo.green.length) << vinfo.green.offset)
							| (blue >> (8 - vinfo.blue.length) << vinfo.blue.offset);
				}
			}

			switch(vinfo.bits_per_pixel)
			{
			case 8:
				if (fillbg || isfg) fb_writeb(dst, color);
				dst++;
				break;
			case 16:
				if (fillbg || isfg) fb_writew(dst, color);
				dst += 2;
				break;
			case 32:
				if (fillbg || isfg) fb_writel(dst, color);
				dst += 4;
				break;
			}
		}
	}

	return glyph->root.advance.x >> 16;
}
