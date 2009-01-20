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

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kd.h>
#include <linux/fb.h>
#include "fbshellman.h"
#include "font.h"
#include "screen.h"
#include "fbconfig.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define redraw(args...) (FbShellManager::instance()->redraw(args))

static const s8 show_cursor[] = "\e[?25h";
static const s8 hide_cursor[] = "\e[?25l";
static const s8 disable_blank[] = "\e[9;0]";
static const s8 enable_blank[] = "\e[9;10]";
static const s8 clear_screen[] = "\e[2J\e[H";

static fb_fix_screeninfo finfo;
static fb_var_screeninfo vinfo;
static u32 bytes_per_pixel;

RotateType Screen::mRotate;
u32 Screen::mScreenw;
u32 Screen::mScreenh;

#include "screen_clip.cpp"
#include "screen_render.cpp"

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
		fprintf(stderr, "can't open framebuffer device!\n");
		return 0;
	}

	fcntl(devFd, F_SETFD, fcntl(devFd, F_GETFD) | FD_CLOEXEC);
	ioctl(devFd, FBIOGET_FSCREENINFO, &finfo);
	ioctl(devFd, FBIOGET_VSCREENINFO, &vinfo);

	if (finfo.type != FB_TYPE_PACKED_PIXELS) {
		fprintf(stderr, "unsupported framebuffer device!\n");
		return 0;
	}

	switch (vinfo.bits_per_pixel) {
	case 8:
		if (finfo.visual != FB_VISUAL_PSEUDOCOLOR) {
			fprintf(stderr, "only support pseudocolor visual with 8bpp depth!\n");
			return 0;
		}
		break;

	case 15:
	case 16:
	case 32:
		if (finfo.visual != FB_VISUAL_TRUECOLOR && finfo.visual != FB_VISUAL_DIRECTCOLOR) {
			fprintf(stderr, "only support truecolor/directcolor visual with 15/16/32bpp depth!\n");
			return 0;
		}
		break;

	default:
		fprintf(stderr, "only support framebuffer device with 8/15/16/32 color depth!\n");
		return 0;
	}

	u32 rtype = 0;
	Config::instance()->getOption("screen-rotate", rtype);
	if (rtype > 3) rtype = 0;
	mRotate = (RotateType)rtype;

	if (mRotate == Rotate0 || mRotate == Rotate180) {
		mScreenw = vinfo.xres;
		mScreenh = vinfo.yres;
	} else {
		mScreenw = vinfo.yres;
		mScreenh = vinfo.xres;
	}
	
	if (!Font::instance()) return 0;

	if (mScreenw / W(1) == 0 || mScreenh / H(1) == 0) {
		fprintf(stderr, "font size is too huge!\n");
		return 0;
	}
	
	if (vinfo.bits_per_pixel == 15) bytes_per_pixel = 2;
	else bytes_per_pixel = (vinfo.bits_per_pixel >> 3);

	initFillDraw();	
	return new Screen(devFd);
}

Screen::Screen(s32 fd)
{
	mFd = fd;
	mpMemStart = (u8 *)mmap(0, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, 0);

	mCols = mScreenw / W(1);
	mRows = mScreenh / H(1);
	mScrollAccel = 0;

	if (mRotate == Rotate0 || mRotate == Rotate180) {
		bool ypan = (vinfo.yres_virtual > vinfo.yres && finfo.ypanstep && !(H(1) % finfo.ypanstep));
		bool ywrap = (finfo.ywrapstep && !(H(1) % finfo.ywrapstep));
		if (ywrap && !(vinfo.vmode & FB_VMODE_YWRAP)) {
			vinfo.vmode |= FB_VMODE_YWRAP;
			ioctl(mFd, FBIOPUT_VSCREENINFO, &vinfo);
			ywrap = (vinfo.vmode & FB_VMODE_YWRAP);
		}

		if ((ypan || ywrap) && !ioctl(mFd, FBIOPAN_DISPLAY, &vinfo)) mScrollAccel = (ywrap ? 2 : 1);
	}

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

bool Screen::move(u16 scol, u16 srow, u16 dcol, u16 drow, u16 w, u16 h)
{
	if (clipEnable || mScrollAccel == 0 || scol != dcol) return false;

	u16 top = MIN(srow, drow), bot = MAX(srow, drow) + h;
	u16 left = scol, right = scol + w;

	u32 noaccel_redraw_area = w * (bot - top - 1);
	u32 accel_redraw_area = mCols * mRows - w * h;

	if (noaccel_redraw_area <= accel_redraw_area) return false;

	s32 yoffset = vinfo.yoffset;

	if (mRotate == Rotate0) yoffset += H((s32)srow - drow);
	else yoffset -= H((s32)srow - drow);
	
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
	
	if (top) redraw(0, 0, mCols, top);
	if (bot < mRows) redraw(0, bot, mCols, mRows - bot);
	if (left > 0) redraw(0, top, left, bot - top - 1);
	if (right < mCols) redraw(right, top, mCols - right, bot - top - 1);

	if (redraw_all) {
		eraseMargin(true, mRows);
	} else {
		eraseMargin(drow > srow, drow > srow ? (drow - srow) : (srow - drow));
	}

	return !redraw_all;
}

void Screen::eraseMargin(bool top, u16 h)
{
	if (mScreenw % W(1)) {
		fillRect(W(mCols), top ? 0 : H(mRows - h), mScreenw % W(1), H(h), 0);
	}

	if (mScreenh % H(1)) {
		fillRect(0, H(mRows), mScreenw, mScreenh % H(1), 0);
	}
}

void Screen::drawUnderline(u16 col, u16 row, u8 color)
{
	fillRect(W(col), H(row + 1) - 1, W(1),  1, color);
}

void Screen::clear(u16 col, u16 row, u16 w, u16 h, u8 color)
{
	fillRect(W(col), H(row), W(w), H(h), color);
}

void Screen::drawText(u16 col, u16 row, u16 w, u8 fc, u8 bc, u16 num, u16 *text, bool *dw)
{
	u32 startx, x = W(col), y = H(row), fw = W(1);

	Rectangle rect = { x, y, x + W(w), y + H(1) };
	u32 state = intersectWithClipRects(rect);

	if (state == InSide) return;
	bool clip = (state == Intersect);

	u16 startnum, *starttext;
	bool *startdw, draw_space = false, draw_text = false;

	for (; num; num--, text++, dw++, x += fw) {
		if (*text == 0x20) {
			if (draw_text) {
				draw_text = false;
			
				if (clip) {
					Rectangle rect = { startx, y, x, y + H(1) };
					state = intersectWithClipRects(rect);
				}

				if (state != InSide) {
					drawGlyphs(startx, y, x - startx, fc, bc, startnum - num, starttext, startdw, state == Intersect);
				}
			}

			if (!draw_space) {
				draw_space = true;
				startx = x;
			}
		} else {
			if (draw_space) {
				draw_space = false;
				fillRect(startx, y, x - startx, H(1), bc, clip);
			}

			if (!draw_text) {
				draw_text = true;
				starttext = text;
				startdw = dw;
				startnum = num;
				startx = x;
			}

			if (*dw) x += fw;
		}
	}

	if (draw_text) {
		if (clip) {
			Rectangle rect = { startx, y, x, y + H(1) };
			state = intersectWithClipRects(rect);
		}

		if (state != InSide) {
			drawGlyphs(startx, y, x - startx, fc, bc, startnum - num, starttext, startdw, state == Intersect);
		}
	} else if (draw_space) {
		fillRect(startx, y, x - startx, H(1), bc, clip);
	}
}

void Screen::drawGlyphs(u32 x, u32 y, u32 w, u8 fc, u8 bc, u16 num, u16 *text, bool *dw, bool clip)
{
	if (Font::instance()->isMonospace()) {
		if (clip) {
			Rectangle rect = { x, y, x, y + H(1) };

			for (; num--; text++, dw++) {
				rect.sx = rect.ex;
				rect.ex += *dw ? W(2) : W(1);
				
				u32 state = intersectWithClipRects(rect);
				if (state != InSide) {
					drawGlyph(rect.sx, y, fc, bc, *text, *dw, true, state == Intersect);
				}
			}
		} else {
			for (; num--; text++, dw++) {
				drawGlyph(x, y, fc, bc, *text, *dw, true, clip);
				x += *dw ? W(2) : W(1);
			}
		}
	} else {
		fillRect(x, y, w, H(1), bc, clip);

		for (; num--; text++, dw++) {
			x += drawGlyph(x, y, fc, bc, *text, *dw, false, clip);
		}
	}
}

void Screen::rotateRect(u32 &x, u32 &y, u32 &w, u32 &h)
{
	u32 tmp;
	switch (mRotate) {
	case Rotate0:
		break;

	case Rotate90:
		tmp = x;
		x = mScreenh - y - h;
		y = tmp;

		tmp = w;
		w = h;
		h = tmp;
		break;

	case Rotate180:
		x = mScreenw - x - w;
		y = mScreenh - y - h;
		break;

	case Rotate270:
		tmp = y;
		y = mScreenw - x - w;
		x = tmp;

		tmp = w;
		w = h;
		h = tmp;
		break;
	}
}

void Screen::rotatePoint(u32 W, u32 H, u32 &x, u32 &y)
{
	u32 tmp;
	switch (mRotate) {
	case Rotate0:
		break;

	case Rotate90:
		tmp = x;
		x = H - y - 1;
		y = tmp;
		break;

	case Rotate180:
		x = W - x - 1;
		y = H - y - 1;
		break;

	case Rotate270:
		tmp = y;
		y = W - x - 1;
		x = tmp;
		break;
	}
}

static inline u32 offsetY(u32 y)
{
	y += vinfo.yoffset;
	if (y >= vinfo.yres_virtual) y -= vinfo.yres_virtual;
	return y;
}

void Screen::fillRect(u32 x, u32 y, u32 w, u32 h, u8 color, bool clip)
{
	if (x >= mScreenw || y >= mScreenh || !w || !h) return;
	if (x + w > mScreenw) w = mScreenw - x;
	if (y + h > mScreenh) h = mScreenh - y;

	rotateRect(x, y, w, h);

	Rectangle rect = { x, y, x + w, y + h }, *rects = &rect;
	u32 num = 1;
	bool isalloc = false;

	if (clip) {
		isalloc = subtractWithRotatedClipRects(rect, rects, num);
	}
	
	for (; num--;) {
		u32 h = rects[num].ey - rects[num].sy;
		u32 w = rects[num].ex - rects[num].sx;
		if (!h || !w) continue;
		
		u8 *dst8 = mpMemStart + rects[num].sx * bytes_per_pixel;
		for(; h--;)
		{
			u8 *dst = dst8 + offsetY(rects[num].sy++) * finfo.line_length;
			fill(dst, w, color);
		}
	}

	if (isalloc) delete[] rects;
}

s16 Screen::drawGlyph(u32 x, u32 y, u8 fc, u8 bc, u16 code, bool dw, bool fillbg, bool clip)
{
	if (x >= mScreenw || y >= mScreenh) return 0;

	s32 w = (dw ? W(2) : W(1)), h = H(1);
	if (x + w > mScreenw) w = mScreenw - x;
	if (y + h > mScreenh) h = mScreenh - y;

	Font::Glyph *glyph = (Font::Glyph *)Font::instance()->getGlyph(code);
	if (!glyph) {
		if (fillbg) fillRect(x, y, w, h, bc, clip);
		return w;
	}

	s32 top = glyph->top;
	if (top < 0) top = 0;

	s32 left = glyph->left;
	if ((s32)x + left < 0) left = -x;
	
	s32 width = glyph->width;
	if (width > w - left) width = w - left;
	if ((s32)x + left + width > (s32)mScreenw) width = mScreenw - ((s32)x + left);
	if (width < 0) width = 0;

	s32 height = glyph->height;
	if (height > h - top) height = h - top;
	if (y + top + height > mScreenh) height = mScreenh - (y + top);
	if (height < 0) height = 0;
	
	if (fillbg) {
		if (top) fillRect(x, y, w, top, bc, clip);
		if (left > 0) fillRect(x, y + top, left, height, bc, clip);
	
		s32 right = width + left;
		if (w > right) fillRect((s32)x + right, y + top, w - right, height, bc, clip);
	
		s32 bot = top + height;
		if (h > bot) fillRect(x, y + bot, w, h - bot, bc, clip);
	}

	x += left;
	y += top;
	if (x >= mScreenw || y >= mScreenh || !width || !height) return glyph->advance;

	u32 nwidth = width, nheight = height;
	rotateRect(x, y, nwidth, nheight);

	u8 *pixmap = glyph->pixmap;
	u32 wdiff = glyph->width - width, hdiff = glyph->height - height;
	
	if (wdiff) {
		if (mRotate == Rotate180) pixmap += wdiff;
		else if (mRotate == Rotate270) pixmap += wdiff * glyph->pitch;
	}
	
	if (hdiff) {
		if (mRotate == Rotate90) pixmap += hdiff;
		else if (mRotate == Rotate180) pixmap += hdiff * glyph->pitch;
	}
	
	Rectangle rect = { x, y, x + nwidth, y + nheight }, *rects = &rect;
	u32 num = 1;
	bool isalloc = false;

	if (clip) {
		isalloc = subtractWithRotatedClipRects(rect, rects, num);
	}

	for (; num--;) {
		u32 w = rects[num].ex - rects[num].sx;
		u32 h = rects[num].ey - rects[num].sy;
		if (!w || !h) continue;

		u8 *pm = pixmap + (rects[num].sy - y) * glyph->pitch + (rects[num].sx - x);
		u8 *dst, *dst_line = mpMemStart + rects[num].sx * bytes_per_pixel;

		for (; h--; pm += glyph->pitch) {
			dst = dst_line + offsetY(rects[num].sy++) * finfo.line_length;
			draw(dst, pm, w, fillbg, fc, bc);
		}
	}
	
	if (isalloc) delete[] rects;
	return glyph->advance;
}
