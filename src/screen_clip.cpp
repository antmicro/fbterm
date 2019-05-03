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

#include <string.h>
#include "immessage.h"

typedef enum { InSide = 0, Intersect, OutSide } IntersectState;

static Rectangle clipRects[NR_IM_WINS], rotatedClipRects[NR_IM_WINS];
static u32 clipRectNum = 0;
bool clipEnable = false;

static inline u32 intersectRect(const Rectangle &R, const Rectangle &r)
{
	if (R.sy >= r.ey || R.ey <= r.sy || R.sx >= r.ex || R.ex <= r.sx) return OutSide;
	if (r.sy >= R.sy && r.ey <= R.ey && r.sx >= R.sx && r.ex <= R.ex) return InSide;
	return Intersect;
}

static u32 subtractRect(const Rectangle &R, Rectangle& r, Rectangle *rects)
{
	if (r.sx >= r.ex || r.sy >= r.ey) return 0;

	u32 add = 0;
	u32 state = intersectRect(R, r);

	if (state == OutSide) ;
	else if (state == Intersect) {
		Rectangle ir = { MAX(R.sx, r.sx), MAX(R.sy, r.sy), MIN(R.ex, r.ex), MIN(R.ey, r.ey) };

		if (ir.sy != r.sy) {
			Rectangle tmp = { r.sx, r.sy, r.ex, ir.sy };
			*rects++ = tmp;
			add++;
		}

		if (ir.ey != r.ey) {
			Rectangle tmp = { r.sx, ir.ey, r.ex, r.ey };
			*rects++ = tmp;
			add++;
		}

		if (ir.sx != r.sx) {
			Rectangle tmp = { r.sx, ir.sy, ir.sx, ir.ey };
			*rects++ = tmp;
			add++;
		}

		if (ir.ex != r.ex) {
			Rectangle tmp = { ir.ex, ir.sy, r.ex, ir.ey };
			*rects++ = tmp;
			add++;
		}

		add--;
		r = *--rects;
	} else if (state == InSide) {
		r.ex = r.sx;
		r.ey = r.sy;
	}

	return add;
}

static u32 intersectWithClipRects(const Rectangle &r)
{
	u32 ret = OutSide;

	for (u32 i = clipRectNum; i--;) {
		u32 state = intersectRect(clipRects[i], r);

		if (ret > state) ret = state;
		if (ret == InSide) break;
	}

	return ret;
}

static bool subtractWithRotatedClipRects(const Rectangle &rect, Rectangle *&rects, u32 &num)
{
	if (!clipRectNum) return false;

	#define NR_SUBTRACT_RECTS 40
	static Rectangle rs[NR_SUBTRACT_RECTS];
	rects = rs;

	rects[0] = rect;
	num = 1;

	u32 size = NR_SUBTRACT_RECTS;
	bool isalloc = false;

	for (u32 i = clipRectNum; i--;) {
		for (u32 j = num; j--;) {
			if (num > size - 4) {
				size *= 2;
				Rectangle *nrects = new Rectangle[size];
				memcpy(nrects, rects, sizeof(Rectangle) * num);

				if (isalloc) delete[] rects;

				isalloc = true;
				rects = nrects;
			}

			num += subtractRect(rotatedClipRects[i], rects[j], rects + num);
		}
	}

	return isalloc;
}

void Screen::setClipRects(Rectangle *rects, u32 num)
{
	if ((!rects && num) || num > NR_IM_WINS) return;
	clipEnable = num;

	Rectangle oldRects[clipRectNum];
	memcpy(oldRects, clipRects, sizeof(Rectangle) * clipRectNum);

	u32 oldNum = clipRectNum;
	clipRectNum = 0;

	for (u32 i = 0; i < num; i++) {
		if (rects[i].sx >= screenw) rects[i].sx = screenw;
		if (rects[i].ex >= screenw) rects[i].ex = screenw;
		if (rects[i].sy >= screenh) rects[i].sy = screenh;
		if (rects[i].ey >= screenh) rects[i].ey = screenh;

		if (rects[i].sx >= rects[i].ex || rects[i].sy >= rects[i].ey) continue;

		clipRects[clipRectNum] = rects[i];

		u32 x = rects[i].sx, y = rects[i].sy;
		u32 w = rects[i].ex - x, h = rects[i].ey - y;

		rotateRect(x, y, w, h);

		rotatedClipRects[clipRectNum].sx = x;
		rotatedClipRects[clipRectNum].sy = y;
		rotatedClipRects[clipRectNum].ex = x + w;
		rotatedClipRects[clipRectNum++].ey = y + h;
	}

	for (u32 i = 0; i < oldNum; i++) {
		if (intersectWithClipRects(oldRects[i]) == InSide) continue;

		u16 scol = oldRects[i].sx / FW(1);
		u16 ecol = (oldRects[i].ex - 1) / FW(1);
		u16 srow = oldRects[i].sy / FH(1);
		u16 erow = (oldRects[i].ey - 1) / FH(1);

		redraw(scol, srow, ecol - scol + 1, erow - srow + 1);

		if (ecol >= mCols) {
			fillRect(FW(mCols), oldRects[i].sy, oldRects[i].ex - FW(mCols), oldRects[i].ey - oldRects[i].sy, 0);
		}

		if (erow >= mRows) {
			fillRect(oldRects[i].sx, FH(mRows), oldRects[i].ex - oldRects[i].sx, oldRects[i].ey - FH(mRows), 0);
		}
	}
}
