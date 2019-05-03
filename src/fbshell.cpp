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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include "fbshell.h"
#include "fbshellman.h"
#include "fbconfig.h"
#include "screen.h"
#include "improxy.h"
#include "fbterm.h"

#define screen (Screen::instance())
#define manager (FbShellManager::instance())
#define improxy (ImProxy::instance())

static const Color defaultPalette[NR_COLORS] = {
	{ 0,	0,		0 },
	{ 0,	0,		0xaa },
	{ 0, 	0xaa,	0 },
	{ 0,	0x55,	0xaa },
	{ 0xaa,	0,		0 },
	{ 0xaa,	0,		0xaa },
	{ 0xaa,	0xaa,	0 },
	{ 0xaa,	0xaa,	0xaa },
	{ 0x55,	0x55,	0x55 },
	{ 0x55,	0x55,	0xff },
	{ 0x55,	0xff,	0x55 },
	{ 0x55,	0xff,	0xff },
	{ 0xff,	0x55,	0x55 },
	{ 0xff,	0x55,	0xff },
	{ 0xff,	0xff,	0x55 },
	{ 0xff,	0xff,	0xff },
};

u16 VTerm::init_history_lines()
{
	u32 val = 1000;
	Config::instance()->getOption("history-lines", val);
	if (val > 65535) val = 65535;
	return val;
}

u8 VTerm::init_default_color(bool foreground)
{
	u32 color;
	
	if (foreground) {
		color = 7;
		Config::instance()->getOption("color-foreground", color);
		if (color > 7) color = 7;
	} else {
		color = 0;
		Config::instance()->getOption("color-background", color);
		if (color > 7) color = 0;
	}

	return color;
}

void Shell::initWordChars(s8 *buf, u32 len)
{
	Config::instance()->getOption("word-chars", buf, len);
}

FbShell::FbShell()
{
	mPaletteChanged = false;
	mPalette = 0;
	createChildProcess();
	resize(screen->cols(), screen->rows());
}

FbShell::~FbShell()
{
	manager->shellExited(this);
	if (mPalette) delete[] mPalette;
}

void FbShell::drawChars(CharAttr attr, u16 x, u16 y, u16 w, u16 num, u16 *chars, bool *dws)
{
	if (manager->activeShell() != this) return;
	adjustCharAttr(attr);
	screen->drawText(x, y, w, attr.fcolor, attr.bcolor, num, chars, dws);
}

bool FbShell::moveChars(u16 sx, u16 sy, u16 dx, u16 dy, u16 w, u16 h)
{
	if (manager->activeShell() != this) return true;
	return screen->move(sx, sy, dx, dy, w, h);
}

void FbShell::drawCursor(CharAttr attr, u16 x, u16 y, u16 c)
{
	u16 oldX = mCursor.x, oldY = mCursor.y;

	adjustCharAttr(attr);
	mCursor.attr = attr;
	mCursor.x = x;
	mCursor.y = y;
	mCursor.code = c;
	mCursor.showed = false;

	updateCursor();
	
	if (manager->activeShell() == this && (oldX != x || oldY != y)) {
		reportCursor();
	}
}

void FbShell::updateCursor()
{
	if (manager->activeShell() != this || mCursor.x >= w() || mCursor.y >= h()) return;
	mCursor.showed ^= true;
	
	u16 shape = mode(CursorShape);
	if (shape == CurDefault) {
		static bool inited = false;
		static u32 default_shape = 0;
		if (!inited) {
			inited = true;
			Config::instance()->getOption("cursor-shape", default_shape);

			if (!default_shape) default_shape = CurUnderline;
			else default_shape = CurBlock;
		}
		
		shape = default_shape;
	}

	switch (shape) {
	case CurNone:
		break;

	case CurUnderline:
		screen->drawUnderline(mCursor.x, mCursor.y, mCursor.showed ? mCursor.attr.fcolor : mCursor.attr.bcolor);
		break;

	default: {
		bool dw = (mCursor.attr.type != CharAttr::Single);

		u16 x = mCursor.x;
		if (mCursor.attr.type == CharAttr::DoubleRight) x--;

		u8 fc = mCursor.attr.fcolor, bc = mCursor.attr.bcolor;
		if (mCursor.showed) {
			u8 temp = fc;
			fc = bc;
			bc = temp;
		}

		screen->drawText(x, mCursor.y, dw ? 2 : 1, fc, bc, 1, &mCursor.code, &dw);
		break;
	}
	}
}

void FbShell::enableCursor(bool enable)
{
	static u32 interval = 500;
	static bool inited = false;
	if (!inited) {
		inited = true;
		Config::instance()->getOption("cursor-interval", interval);
	}
	
	if (!interval) return;
	
	static bool enabled = false;
	if (enabled == enable) return;
	enabled = enable;
	
	u32 val = (enable ? interval : 0);
	u32 sec = val / 1000, usec = (val % 1000) * 1000;

	struct itimerval timer;
	timer.it_interval.tv_usec = usec;
	timer.it_interval.tv_sec = sec;
	timer.it_value.tv_usec = usec;
	timer.it_value.tv_sec = sec;
	
	setitimer(ITIMER_REAL, &timer, NULL);
}

void FbShell::modeChanged(ModeType type)
{
	if (manager->activeShell() != this) return;

	if (type & (CursorVisible | CursorShape)) {
		enableCursor(mode(CursorVisible) && mode(CursorShape) != CurNone);
	}
	
	if (type & CursorKeyEscO) {
		changeMode(CursorKeyEscO, mode(CursorKeyEscO));
	}

	if (type & AutoRepeatKey) {
		changeMode(AutoRepeatKey, mode(AutoRepeatKey));
	}

	if (type & ApplicKeypad) {
		changeMode(ApplicKeypad, mode(ApplicKeypad));
	}
	
	if (type & CRWithLF) {
		changeMode(CRWithLF, mode(CRWithLF));
	}
	
	if (type & (CursorKeyEscO | ApplicKeypad | CRWithLF)) {
		reportMode();
	}
}

void FbShell::request(RequestType type,  u32 val)
{
	bool active = (manager->activeShell() == this);

	switch (type) {
	case PaletteSet: 
		if ((val >> 24) >= NR_COLORS) break;
		
		if (!mPaletteChanged) {
			mPaletteChanged = true;
			
			if (!mPalette) mPalette = new Color[NR_COLORS];
			memcpy(mPalette, defaultPalette, sizeof(defaultPalette));
		}
		
		mPalette[val >> 24].red = (val >> 16) & 0xff;
		mPalette[val >> 24].green = (val >> 8) & 0xff;
		mPalette[val >> 24].blue = val & 0xff;

		if (active) {
			screen->setPalette(mPalette);
		}
		break;
	
	case PaletteClear:
		if (!mPaletteChanged) break;
		mPaletteChanged = false;
		
		if (active) {
			screen->setPalette(defaultPalette);
		}
		break;

	case VcSwitch:
		break;

	default:
		break;
	}
}

void FbShell::switchVt(bool enter, FbShell *peer)
{
	if (enter) {
		screen->setPalette(mPaletteChanged ? mPalette : defaultPalette);
		modeChanged(AllModes);
	} else if (!peer) {
		changeMode(CursorKeyEscO, false);
		changeMode(ApplicKeypad, false);
		changeMode(CRWithLF, false);
		changeMode(AutoRepeatKey, true);

		enableCursor(false);
		screen->setPalette(defaultPalette);
	}
}

void FbShell::initChildProcess()
{
	FbTerm::instance()->initChildProcess();
}

void FbShell::switchCodec(u8 index)
{
	if (!index) {
		setCodec("UTF-8", localCodec());
		return;
	}

	#define NR_CODECS 5
	if (index > NR_CODECS) return;

	static s8 buf[128], *codecs[NR_CODECS];
	static bool inited = false;
	if (!inited) {
		inited = true;
		Config::instance()->getOption("text-encodings", buf, sizeof(buf));
		if (!*buf) return;

		s8 *cur = buf, *next;
		u8 i = 0;
		while (1) {
			next = strchr(cur, ',');
			codecs[i++] = cur;

			if (!next) break;
			*next = 0;

			if (i == NR_CODECS) break;

			cur = next + 1;
		}
	}

	if (codecs[index - 1]) {
		setCodec("UTF-8", codecs[index - 1]);
	}
}

void FbShell::keyInput(s8 *buf, u32 len)
{
	clearMousePointer();
	Shell::keyInput(buf, len);
}

void FbShell::mouseInput(u16 x, u16 y, s32 type, s32 buttons)
{
	if (type == Move) {
		clearMousePointer();
	
		CharAttr attr = charAttr(x, y);
		adjustCharAttr(attr);
		
		bool dw = (attr.type != CharAttr::Single);
		u16 code = charCode(x, y);

		if (attr.type == CharAttr::DoubleRight) x--;
		screen->drawText(x, y, dw ? 2 : 1, attr.bcolor, attr.fcolor, 1, &code, &dw);
		
		mMousePointer.x = x;
		mMousePointer.y = y;
		mMousePointer.drawed = true;
	}
	
	Shell::mouseInput(x, y, type, buttons);
}

void FbShell::readyRead(s8 *buf, u32 len)
{
	clearMousePointer();
	Shell::readyRead(buf, len);
}

void FbShell::clearMousePointer()
{
	if (mMousePointer.drawed) {
		mMousePointer.drawed = false;
		expose(mMousePointer.x, mMousePointer.y, 1, 1);
	}
}

void FbShell::expose(u16 x, u16 y, u16 w, u16 h)
{
	VTerm::expose(x, y, w, h);

	if (mode(CursorVisible) && mCursor.y >= y && mCursor.y < (y + h) && mCursor.x >= x && mCursor.x < (x + w)) {
		mCursor.showed = false;
		updateCursor();
	}
}

void FbShell::adjustCharAttr(CharAttr &attr)
{
	if (attr.italic) attr.fcolor = 2; // green
	else if (attr.underline) attr.fcolor = 6; // cyan
	else if (attr.intensity == 0) attr.fcolor = 8; // gray

	if (attr.blink) attr.bcolor ^= 8;
	if (attr.intensity == 2) attr.fcolor ^= 8;

    if (attr.reverse) {
		u16 temp = attr.bcolor;
		attr.bcolor = attr.fcolor;
		attr.fcolor = temp;

		if (attr.bcolor > 8) attr.bcolor -= 8;
	}
}

void FbShell::changeMode(ModeType type, u16 val)
{
	const s8 *str = 0;

	if (type == CursorKeyEscO) str = (val ? "\e[?1h" : "\e[?1l");
	else if (type == AutoRepeatKey) str = (val ? "\e[?8h" : "\e[?8l");
	else if (type == ApplicKeypad) str = (val ? "\e=" : "\e>");
	else if (type == CRWithLF) str = (val ? "\e[20h" : "\e[20l");

	if (str) {
		s32 ret = ::write(STDIN_FILENO, str, strlen(str));
	}
}

void FbShell::reportCursor()
{
	improxy->changeCursorPos(mCursor.x, mCursor.y);
}

void FbShell::reportMode()
{
	improxy->changeTermMode(mode(CRWithLF), mode(ApplicKeypad), mode(CursorKeyEscO));
}
