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
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include "fbshell.h"
#include "fbconfig.h"
#include "fbterm.h"
#include "screen.h"

u16 VTerm::init_history_lines()
{
	s32 val = 1000;
	Config::instance()->getOption("history_lines", val);

	if (val < 0) val = 0;
	if (val > 65535) val = 65535;

	return val;
}

void Shell::initDefaultColor()
{
	s32 color = -1;
	Config::instance()->getOption("color_foreground", color);
	if (color < 0 || color > 7) color = 7;
	default_fcolor = color;

	color = -1;
	Config::instance()->getOption("color_background", color);
	if (color < 0 || color > 7) color = 0;
	default_bcolor = color;
}

void FbShell::switchCodec(u8 index)
{
	if (!index) {
		setCodec("UTF-8", localCodec());
		return;
	}

	if (index > 5) return;

	static s8 buf[128], *codecs[5];
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

			if (i == 5) break;

			cur = next + 1;
		}
	}

	if (codecs[index - 1]) {
		setCodec("UTF-8", codecs[index - 1]);
	}
}

#define screen (Screen::instance())
#define NR_SHELLS 10

bool FbShell::mVcCurrent = true;
u8 FbShell::mShellCount = 0;
u8 FbShell::mCurShell = 0;
FbShell *FbShell::mShellList[NR_SHELLS];
FbShell *FbShell::mActiveShell = 0;

FbShell *FbShell::createShell()
{
	if (mShellCount == NR_SHELLS) return 0;
	mShellCount++;

	FbShell *shell = new FbShell();
	return shell;
}

void FbShell::deleteShell()
{
	if (mShellList[mCurShell]) delete mShellList[mCurShell];
}

void FbShell::nextShell()
{
	if (!mShellCount || (mShellCount == 1 && mShellList[mCurShell])) return;

	u8 index = mCurShell;
	for (u8 i = 0; i < NR_SHELLS; i++) {
		index++;
		index %= NR_SHELLS;
		if (mShellList[index]) break;
	}
	switchShell(index);
}

void FbShell::prevShell()
{
	if (!mShellCount || (mShellCount == 1 && mShellList[mCurShell])) return;

	u8 index = mCurShell;
	for (u8 i = 0; i < NR_SHELLS; i++) {
		if (!index) index = NR_SHELLS;
		index--;
		if (mShellList[index]) break;
	}
	switchShell(index);
}

void FbShell::switchShell(u8 num)
{
	mCurShell = num;
	if (mVcCurrent) setActive(mShellList[mCurShell]);
}

void FbShell::setActive(FbShell *shell)
{
	if (mActiveShell == shell) return;

	if (mActiveShell) {
		mActiveShell->enterLeave(false, (Shell*)shell);
	}

	FbShell *prev = mActiveShell;
	mActiveShell = shell;
	if (shell) {
		shell->enterLeave(true, prev);
	}

	redraw(0, 0, screen->cols(), screen->rows());
}

FbShell::FbShell()
{
	u8 index = mCurShell;
	for (u8 i = 0; i < NR_SHELLS; i++) {
		if (!mShellList[index]) break;
		index++;
		index %= NR_SHELLS;
	}
	mShellList[index] = this;
	switchShell(index);

	createChildProcess();
	resize(screen->cols(), screen->rows());
}

FbShell::~FbShell()
{
	u8 index = mCurShell;
	if (mShellList[mCurShell] != this) {
		for (index = 0; index < NR_SHELLS; index++) {
			if (mShellList[index] == this) break;
		}
	}

	if (index == mCurShell) {
		prevShell();
	}

	mShellList[index] = 0;

	if (!--mShellCount) {
		enterLeave(false, 0);
		mActiveShell = 0;
		FbTerm::instance()->exit();
	}
}

void FbShell::drawChars(CharAttr attr, u16 x, u16 y, u16 num, u16 *chars, bool *dws)
{
	if (mActiveShell != this) return;
	adjustCharAttr(attr);
	screen->drawText(x, y, attr.fcolor, attr.bcolor, num, chars, dws);
}

bool FbShell::moveChars(u16 sx, u16 sy, u16 dx, u16 dy, u16 w, u16 h)
{
	if (mActiveShell != this) return true;
	return screen->move(sx, sy, dx, dy, w, h);
}

void FbShell::drawCursor(u16 x, u16 y, u8 color)
{
	if (mActiveShell != this) return;
	screen->drawUnderline(x, y, color);
}

void FbShell::drawCursor()
{
	if (mActiveShell) {
		mActiveShell->updateCursor();
	}
}

void FbShell::redraw(u16 x, u16 y, u16 w, u16 h)
{
	if (mActiveShell) {
		mActiveShell->expose(x, y, w, h);
	} else {
		screen->clear(x, y, w, h, 0);
	}
}

void FbShell::enableCursor(bool enable)
{
	if (mActiveShell != this) return;

	static bool enabled = false;
	if (enabled == enable) return;
	enabled = enable;

	u32 interval = (enable ? 500000 : 0);

	struct itimerval timer;
	timer.it_interval.tv_usec = interval;
	timer.it_interval.tv_sec = 0;
	timer.it_value.tv_usec = interval;
	timer.it_value.tv_sec = 0;
	
	setitimer(ITIMER_REAL, &timer, NULL);
}

void FbShell::enterLeaveVc(bool enter)
{
	mVcCurrent = enter;
	setActive(enter ? mShellList[mCurShell] : 0);
}

void FbShell::initChildProcess()
{
	setuid(getuid());

#ifdef SYS_signalfd
	extern sigset_t oldSigmask;
	sigprocmask(SIG_SETMASK, &oldSigmask, 0);
#endif
}
