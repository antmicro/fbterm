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

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_GLYPH_H
#include "font.h"
#include "fbconfig.h"

Font::Glyph::~Glyph()
{
	FT_Done_Glyph(FT_Glyph(glyph));
}

DEFINE_INSTANCE(Font)

Font *Font::createInstance()
{
	FcInit();

	s8 buf[64];
	Config::instance()->getOption("font_family", buf, sizeof(buf));

	FcPattern *pat = FcNameParse((FcChar8*)buf);

	s32 pixel_size = 12;
	Config::instance()->getOption("font_size", pixel_size);
	FcPatternAddDouble(pat, FC_PIXEL_SIZE, (double)pixel_size);

	FcPatternAddString(pat, FC_LANG, (FcChar8*)"en");

	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);

	FcResult result;
	FcCharSet *cs;
	FcFontSet *fs = FcFontSort(NULL, pat, FcTrue, &cs, &result);

	FontRec *fonts;
	u32 index = 0;
	if (fs) {
		fonts = new FontRec[fs->nfont];
	
		for (s32 i = 0; i < fs->nfont; i++) {
			FcPattern *font = FcFontRenderPrepare(NULL, pat, fs->fonts[i]);
			if (font) {
				fonts[index].face = 0;
				fonts[index++].pattern = font;
			}
		}
	}

	FcPatternDestroy(pat);
	if (fs) FcFontSetDestroy(fs);

	if (!index) {
		if (fs) {
			FcCharSetDestroy(cs);
			delete[] fonts;
		}

		FcFini();
		return 0;
	}

	return new Font(fonts, index, cs);
}

static FT_Library ftlib = 0;

Font::Font(FontRec *fonts, u32 num, void *unicover)
{
	mpFontList = fonts;
	mFontNum = num;
	mpUniCover = unicover;
	mpFontCache = new HashTable<Glyph*>(1024, true);
	mMonospace = false;

	s32 spacing;
	if (FcPatternGetInteger((FcPattern*)mpFontList[0].pattern, FC_SPACING, 0, &spacing) == FcResultMatch)
		mMonospace = (spacing != FC_PROPORTIONAL);

	FT_Init_FreeType(&ftlib);

	openFont(0);
	mHeight = ((FT_Face)mpFontList[0].face)->size->metrics.height >> 6;
	mWidth = ((FT_Face)mpFontList[0].face)->size->metrics.max_advance >> 6;

	#define ABS(a, b) ((a) > (b) ? ((a) - (b)) : ((b) - (a)))
	if (ABS(mHeight >> 1, mWidth >> 1) < ABS(mHeight >> 1, mWidth)) mWidth = (mWidth + 1) >> 1;
}

Font::~Font()
{
	FcCharSetDestroy((FcCharSet*)mpUniCover);

	for (u32 i = 0; i < mFontNum; i++) {	
		FcPatternDestroy((FcPattern*)mpFontList[i].pattern);

		FT_Face face = (FT_Face)mpFontList[i].face;
		if (face > 0)
			FT_Done_Face(face);
	}

	delete mpFontCache;
	delete[] mpFontList;	

	FT_Done_FreeType(ftlib);
	FcFini();
}

void Font::openFont(u32 index)
{
	if (index >= mFontNum) return;

	FcPattern *pattern = (FcPattern*)mpFontList[index].pattern;

	FcChar8 *name = (FcChar8*)"";
	FcPatternGetString(pattern, FC_FILE, 0, &name);

	s32 id = 0;
	FcPatternGetInteger (pattern, FC_INDEX, 0, &id);

	FT_Face face;
	if (FT_New_Face(ftlib, (const char*)name, id, &face)) {
		mpFontList[index].face = (void*)-1;
		return;
	}

	double ysize;
	FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &ysize);
	FT_Set_Pixel_Sizes(face, 0, (FT_UInt)ysize);

	s32 load_flags = FT_LOAD_DEFAULT;

	FcBool scalable, antialias;
	FcPatternGetBool(pattern, FC_SCALABLE, 0, &scalable);
	FcPatternGetBool(pattern, FC_ANTIALIAS, 0, &antialias);

	if (scalable && antialias) load_flags |= FT_LOAD_NO_BITMAP;

	if (antialias) {
		FcBool hinting;
		s32 hint_style;
		FcPatternGetBool(pattern, FC_HINTING, 0, &hinting);
		FcPatternGetInteger(pattern, FC_HINT_STYLE, 0, &hint_style);

		if (!hinting || hint_style == FC_HINT_NONE) {
			load_flags |= FT_LOAD_NO_HINTING;
		} else { 
			load_flags |= FT_LOAD_TARGET_LIGHT;
		}
	} else {
		load_flags |= FT_LOAD_TARGET_MONO;
	}

	mpFontList[index].face = face;
	mpFontList[index].load_flags = load_flags;
}

s32 Font::fontIndex(u32 unicode)
{
	if (!FcCharSetHasChar((FcCharSet*)mpUniCover, (FcChar32)unicode)) return -1;

	FcCharSet *charset;
	for (u32 i = 0; i < mFontNum; i++) {
		FcPatternGetCharSet((FcPattern*)mpFontList[i].pattern, FC_CHARSET, 0, &charset);
		if (FcCharSetHasChar(charset, unicode)) return i;
	}

	return -1;
}

void *Font::getGlyph(u32 unicode)
{
	Glyph *glyph;
	mpFontCache->find(unicode, &glyph);

	if (glyph) return glyph;

	s32 i = fontIndex(unicode);
	if (i == -1) return 0;

	if (!mpFontList[i].face) openFont(i);
	if (mpFontList[i].face == (void*)-1) return 0;

	FT_Face face = (FT_Face)mpFontList[i].face;
	FT_UInt index = FT_Get_Char_Index(face, (FT_ULong)unicode);
	if (!index) return 0;

	FT_Glyph ftglyph;
	FT_Load_Glyph(face, index, FT_LOAD_RENDER | mpFontList[i].load_flags);
	FT_Get_Glyph(face->glyph, &ftglyph);

	glyph = new Glyph(ftglyph, mHeight - 1 + (face->size->metrics.descender >> 6));
	mpFontCache->add(unicode, glyph);

	return glyph;
}
