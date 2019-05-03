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

#ifndef FONT_H
#define FONT_H

#include "hash.h"
#include "instance.h"

class Font {
	DECLARE_INSTANCE(Font)
public:
	class Glyph {
	public:
		Glyph(void *_glyph, s16 _baseline) {
			glyph = _glyph;
			baseline = _baseline;
		}
		~Glyph();

		void *glyph;
		s16 baseline;
	};

	void *getGlyph(u32 unicode);
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
	typedef struct {
		void *pattern;
		void *face;
		s32 load_flags;
	} FontRec;

	Font(FontRec *fonts, u32 num, void *unicover);
	void openFont(u32 index);
	s32 fontIndex(u32 unicode);

	void *mpUniCover;
	FontRec *mpFontList;
	u32 mFontNum;
	HashTable<Glyph*> *mpFontCache;
	u32 mWidth, mHeight;
	bool mMonospace;
};

#endif
