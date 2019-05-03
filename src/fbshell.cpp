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
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include "fbshell.h"
#include "fbconfig.h"
#include "fbterm.h"
#include "screen.h"

#define manager (FbShellManager::instance())
#define screen (Screen::instance())

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
	Config::instance()->getOption("history_lines", val);
	if (val > 65535) val = 65535;
	return val;
}

u8 Shell::initDefaultColor(bool foreground)
{
	u32 color;
	
	if (foreground) {
		color = 7;
		Config::instance()->getOption("color_foreground", color);
		if (color > 7) color = 7;
	} else {
		color = 0;
		Config::instance()->getOption("color_background", color);
		if (color > 7) color = 0;
	}

	return color;
}

void Shell::initWordChars(s8 *buf, u32 len)
{
	Config::instance()->getOption("word_chars", buf, len);
}

FbShell::FbShell()
{
	mPaletteChanged = false;
	memcpy(mPalette, defaultPalette, sizeof(mPalette));
	createChildProcess();
	resize(screen->cols(), screen->rows());
}

FbShell::~FbShell()
{
	manager->shellExited(this);
}

void FbShell::drawChars(CharAttr attr, u16 x, u16 y, u16 num, u16 *chars, bool *dws)
{
	if (manager->activeShell() != this) return;
	adjustCharAttr(attr);
	screen->drawText(x, y, attr.fcolor, attr.bcolor, num, chars, dws);
}

bool FbShell::moveChars(u16 sx, u16 sy, u16 dx, u16 dy, u16 w, u16 h)
{
	if (manager->activeShell() != this) return true;
	return screen->move(sx, sy, dx, dy, w, h);
}

void FbShell::drawCursor(CharAttr attr, u16 x, u16 y, u16 c)
{
	adjustCharAttr(attr);
	mCursor.attr = attr;
	mCursor.x = x;
	mCursor.y = y;
	mCursor.code = c;
	mCursor.showed = false;

	updateCursor();
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
			Config::instance()->getOption("cursor_shape", default_shape);

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

		screen->drawText(x, mCursor.y, fc, bc, 1, &mCursor.code, &dw);
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
		Config::instance()->getOption("cursor_interval", interval);
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
		s32 ret = ::write(STDIN_FILENO, mode(CursorKeyEscO) ? "\033[?1h" : "\033[?1l", 5);
	}

	if (type & AutoRepeatKey) {
		s32 ret = ::write(STDIN_FILENO, mode(AutoRepeatKey) ? "\033[?8h" : "\033[?8l", 5);
	}

	if (type & ApplicKeypad) {
		s32 ret = ::write(STDIN_FILENO, mode(ApplicKeypad) ? "\033=" : "\033>", 2);
	}
}

void FbShell::request(RequestType type,  u32 val)
{
	bool active = (manager->activeShell() == this);

	switch (type) {
	case PaletteSet: 
		if ((val >> 24) >= NR_COLORS) break;

		mPaletteChanged = true;
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
		memcpy(mPalette, defaultPalette, sizeof(mPalette));

		if (active) {
			screen->setPalette(mPalette);
		}
		break;

	case VcSwitch:
		break;

	default:
		break;
	}
}

void FbShell::switchVt(bool enter)
{
	Shell::switchVt(enter);

	if (mPaletteChanged) {
		screen->setPalette(enter ? mPalette : defaultPalette);
	}

	if (enter) {
		modeChanged(AllModes);
	} else {
		enableCursor(false);
	}
}

void FbShell::initChildProcess()
{
	setuid(getuid());

#ifdef SYS_signalfd
	extern sigset_t oldSigmask;
	sigprocmask(SIG_SETMASK, &oldSigmask, 0);
#endif
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
		Config::instance()->getOption("text_encoding", buf, sizeof(buf));
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


DEFINE_INSTANCE_DEFAULT(FbShellManager)

FbShellManager::FbShellManager()
{
	mVcCurrent = true;
	mShellCount = 0;
	mCurShell = 0;
	mActiveShell = 0;
	bzero(mShellList, sizeof(mShellList));
	
	screen->setPalette(defaultPalette);
}

FbShellManager::~FbShellManager()
{
}

#define SHELL_ANY ((FbShell *)-1)
u32 FbShellManager::getIndex(FbShell *shell, bool forward,  bool stepfirst)
{
	u32 index, temp = mCurShell + NR_SHELLS;

	#define STEP() do { \
		if (forward) temp++; \
		else temp--; \
	} while (0)

	if (stepfirst) STEP();

	for (u32 i = 0; i < NR_SHELLS; i++) {
		index = temp % NR_SHELLS;
		if ((shell == SHELL_ANY && mShellList[index]) || shell == mShellList[index]) break;
		STEP();
	}

	return index;
}

void FbShellManager::createShell()
{
	if (mShellCount == NR_SHELLS) return;
	mShellCount++;

	u32 index = getIndex(0, true,  false); 
	mShellList[index] = new FbShell();
	switchShell(index);
}

void FbShellManager::deleteShell()
{
	if (mActiveShell) delete mActiveShell;
}

void FbShellManager::shellExited(FbShell *shell)
{
	if (!shell) return;

	u8 index = getIndex(shell, true, false);
	mShellList[index] = 0;

	if (index == mCurShell) {
		prevShell();
	}

	if (!--mShellCount) {
		FbTerm::instance()->exit();
	}
}

void FbShellManager::nextShell()
{
	switchShell(getIndex(SHELL_ANY, true, true));
}

void FbShellManager::prevShell()
{
	switchShell(getIndex(SHELL_ANY, false, true));
}

void FbShellManager::switchShell(u32 num)
{
	if (num >= NR_SHELLS) return;

	mCurShell = num;
	if (mVcCurrent) {
		setActive(mShellList[mCurShell]);
		redraw(0, 0, screen->cols(), screen->rows());
	}	
}

void FbShellManager::setActive(FbShell *shell)
{
	if (mActiveShell == shell) return;

	if (mActiveShell) {
		mActiveShell->switchVt(false);
	}

	mActiveShell = shell;
	if (shell) {
		shell->switchVt(true);
	}
}

void FbShellManager::drawCursor()
{
	if (mActiveShell) {
		mActiveShell->updateCursor();
	}
}

void FbShellManager::redraw(u16 x, u16 y, u16 w, u16 h)
{
	if (mActiveShell) {
		mActiveShell->expose(x, y, w, h);
		
		if (mActiveShell->mode(VTerm::CursorVisible)
			&& mActiveShell->mCursor.x >= x && mActiveShell->mCursor.x < (x + w)
			&& mActiveShell->mCursor.y >= y && mActiveShell->mCursor.y < (y + h)) {
			
			mActiveShell->mCursor.showed = false;
			mActiveShell->updateCursor();
		}
	} else {
		screen->clear(x, y, w, h, 0);
	}
}

void FbShellManager::historyScroll(bool down)
{
	if (!mActiveShell) return;

	mActiveShell->historyDisplay(false, down ? mActiveShell->h() : -mActiveShell->h());

	if (mActiveShell->mode(VTerm::CursorVisible)) {
		mActiveShell->mCursor.showed = false;
		mActiveShell->updateCursor();
	}
}

void FbShellManager::switchVc(bool enter)
{
	mVcCurrent = enter;
	setActive(enter ? mShellList[mCurShell] : 0);

	if (enter) {
		redraw(0, 0, screen->cols(), screen->rows());
	}
}
