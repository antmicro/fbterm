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

#ifndef SCREEN_H
#define SCREEN_H

#include "type.h"
#include "instance.h"

class Screen
{
	DECLARE_INSTANCE(Screen)
public :
	u16 cols() { return mCols; }
	u16 rows() { return mRows; }

	void drawUnderline(u16 col, u16 row, u8 color);
	void clear(u16 col, u16 row, u16 w, u16 h, u8 color);
	void drawText(u16 col, u16 row, u8 fc, u8 bc, u16 num, u16 *text, bool *dw);
	bool move(u16 scol, u16 srow, u16 dcol, u16 drow, u16 w, u16 h);
	void enterLeaveVc(bool enter);

private:
	Screen(s32 fd);
	void setupPalette(bool restore);
	void eraseMargin(bool top, u16 h);

	void fillRect(u32 x, u32 y, u32 w, u32 h, u8 color);
	void drawGlyphs(u32 x, u32 y, u8 fc, u8 bc, u16 num, u16 *text, bool *dw);
	u32 drawGlyph(u32 x, u32 y, u8 fc, u8 bc, u16 code, bool dw, bool fillbg);

	static const struct Color {
		u8 blue, green, red;
	} mColorTable[];

	u16 mCols, mRows;
	s8 mScrollAccel; // 0 = none, 1 = ypan, 2 = ywrap
	s32 mFd;
	s8* mpMemStart;
};

#endif
