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

#ifndef FONT_H
#define FONT_H

#include "type.h"
#include "instance.h"

#define W(col) (Font::instance()->width() * (col))
#define H(row) (Font::instance()->height() * (row))

class Font {
	DECLARE_INSTANCE(Font)
public:
	struct Glyph {
		s16 pitch, width, height;
		s16 left, top, advance;
		bool isbitmap;
		u8 pixmap[0];
	};

	Glyph *getGlyph(u32 unicode);
	u32 width() {
		return mWidth;
	}
	u32 height() {
		return mHeight;
	}
	bool isMonospace() {
		return mMonospace;
	}

private:
	struct FontRec {
		void *pattern;
		void *face;
		int load_flags;
	};

	Font(FontRec *fonts, u32 num, void *unicover);
	void openFont(u32 index);
	int fontIndex(u32 unicode);

	void *mpUniCover;
	FontRec *mpFontList;
	u32 mFontNum;
	u32 mWidth, mHeight;
	bool mMonospace;
};

#endif
