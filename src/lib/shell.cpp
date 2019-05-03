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

#include "config.h"
#include <pty.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include "shell.h"

u8 Shell::default_fcolor;
u8 Shell::default_bcolor;
Shell::SelectedText Shell::mSelText;

Shell::Shell()
{
	static bool inited = false;
	if (!inited) {
		inited = true;
		initDefaultColor();
	}
	
	mInverseText = false;
	mPid = -1;
}

Shell::~Shell()
{
	if (mPid > 0) {
		setFd(-1);
		waitpid(mPid, 0, 0);
	}
}

void Shell::createChildProcess()
{
	s32 fd;
	mPid = forkpty(&fd, NULL, NULL, NULL);

	if (mPid == 0) {  // child process
		initChildProcess();

		setenv("TERM", "linux", 1);
		struct passwd *userpd;
		userpd = getpwuid(getuid());

		execlp(userpd->pw_shell, userpd->pw_shell, NULL);
	}

	setFd(fd);
	setCodec("UTF-8", localCodec());
}

void Shell::readyRead(s8 *buf, u32 len)
{
	clearMousePointer();
	resetTextSelect();
	input((const u8 *)buf, len);
}

void Shell::sendBack(const s8 *data)
{
	write((s8*)data, strlen(data));
}

void Shell::resize(u16 w, u16 h)
{
	if (!w || !h || (w == VTerm::w() && h == VTerm::h())) return;

	clearMousePointer();
	resetTextSelect();

	struct winsize size;
	size.ws_xpixel = 0;
	size.ws_ypixel = 0;
	size.ws_col = w;
	size.ws_row = h;
	ioctl(fd(), TIOCSWINSZ, &size);

	VTerm::resize(w, h);
}

void Shell::keyInput(s8 *buf, u32 len)
{
	clearMousePointer();
	resetTextSelect();
	write(buf, len);
}

void Shell::mouseInput(u16 x, u16 y, s32 type, s32 buttons)
{
	if (x >= w()) x = w() - 1;
	if (y >= h()) y = h() - 1;

	s32 btn = buttons & MouseButtonMask;
	s32 modifies = buttons & ModifyButtonMask;

	if (type == Move && btn == NoButton) {
		drawMousePointer(x, y);
		return;
	}

	bool x10mouse = mode(X10MouseReport);
	bool x11mouse = mode(X11MouseReport);

	if ((!x11mouse && !x10mouse) || (modifies & ShiftButton)) {
		textSelect(x, y, type, btn);
		return;
	}

	s32 val = -1;

	switch (type) {
	case Press:
	case DblClick:
		if (btn & LeftButton) val = 0;
		else if (btn & MidButton) val = 1;
		else if (btn & RightButton) val = 2;
		break;
	case Release:
		if (x11mouse) val = 3;
		break;
	default:
		break;
	}

	if (x11mouse && val != -1) {
		if (modifies & ShiftButton)	val |= 4;
		if (modifies & AltButton) val |= 8;
		if (modifies & ControlButton) val |= 16;
	}

	if (val != -1) {
		s8 buf[8];
		snprintf(buf, sizeof(buf), "\033[M%c%c%c", ' ' + val, ' ' + x + 1, ' ' + y + 1);
		sendBack(buf);
	}
}

void Shell::textSelect(u16 x, u16 y, s32 type, s32 btn)
{
	if (!mSelState.selecting) {
		resetTextSelect();
	}

	if (btn == LeftButton) {
		switch (type) {
		case Press:
			mSelState.selecting = true;
			startTextSelect(x, y);
			break;
		case Move:
			if (mSelState.selecting) {
				middleTextSelect(x, y);
			} else {
				mSelState.selecting = true;
				startTextSelect(x, y);
			}
			break;
		case Release:
			mSelState.selecting = false;
			endTextSelect();
			break;
		case DblClick:
			autoTextSelect(x, y);
			break;
		default:
			break;
		}
	} else if (btn == RightButton) {
		if (type == Press) {
			putSelectedText();
		}
	}
}

void Shell::startTextSelect(u16 x, u16 y)
{
	mSelState.start = mSelState.end = y * w() + x;
	if (charAttr(x, y).type == CharAttr::DoubleLeft) mSelState.end++;
	else if (charAttr(x, y).type == CharAttr::DoubleRight) mSelState.start--;

	changeTextColor(mSelState.start, mSelState.end, true);
	mSelState.color_inversed = true;
	mSelState.positive_direction = true;
}

void Shell::middleTextSelect(u16 x, u16 y)
{
	u32 pos, pos_as_end;
	pos = pos_as_end = y * w() + x;
	if (charAttr(x, y).type == CharAttr::DoubleLeft) pos_as_end++;
	else if (charAttr(x, y).type == CharAttr::DoubleRight) pos--;

	x = mSelState.start % w();
	y = mSelState.start / w();

	u32 start, start_as_end;
	start = start_as_end = y * w() + x;
	if (charAttr(x, y).type == CharAttr::DoubleLeft) start_as_end++;
	else if (charAttr(x, y).type == CharAttr::DoubleRight) start--;

	x = mSelState.end % w();
	y = mSelState.end / w();

	u32 end, end_as_end;
	end = end_as_end = y * w() + x;
	if (charAttr(x, y).type == CharAttr::DoubleLeft) end_as_end++;
	else if (charAttr(x, y).type == CharAttr::DoubleRight) end--;


	if (mSelState.positive_direction) {
		if (pos < start) {
			changeTextColor(start, end_as_end, false);
			changeTextColor(pos, start_as_end, true);

			mSelState.start = pos;
			mSelState.end = start_as_end;
			mSelState.positive_direction = false;
		} else {
			if (pos_as_end > end_as_end) {
				changeTextColor(end_as_end + 1, pos_as_end, true);
			} else if (pos_as_end < end) {
				changeTextColor(pos_as_end + 1, end_as_end, false);
			}

			mSelState.end = pos_as_end;
		}
	} else {
		if (pos_as_end >= end_as_end) {
			changeTextColor(start, end_as_end, false);
			changeTextColor(end, pos_as_end, true);

			mSelState.start = end;
			mSelState.end = pos_as_end;
			mSelState.positive_direction = true;
		} else {
			if (pos < start) {
				changeTextColor(pos, start - 1, true);
			} else if (pos > start_as_end) {
				changeTextColor(start, pos - 1, false);
			}

			mSelState.start = pos;
		}
	}
}

static void utf16_to_utf8(u16 *buf16, u32 num, s8 *buf8)
{
	u16 code;
	u32 index = 0;
	for (; num; num--, buf16++) {
		code = *buf16;
		if (code >> 11) {
			buf8[index++] = 0xe0 | (code >> 12);
			buf8[index++] = 0x80 | ((code >> 6) & 0x3f);
			buf8[index++] = 0x80 | (code & 0x3f);
		} else if (code >> 7) {
			buf8[index++] = 0xc0 | ((code >> 6) & 0x1f);
			buf8[index++] = 0x80 | (code & 0x3f);
		} else {
			buf8[index++] = code;
		}
	}

	buf8[index] = 0;
}

void Shell::endTextSelect()
{
	mMousePointer.drawed = false;

	u32 len = mSelState.end - mSelState.start + 1;
	u16 buf[len];
	s8 *text = new s8[len * 3];

	u16 startx, starty, endx, endy;
	startx = mSelState.start % w(), starty = mSelState.start / w();
	endx = mSelState.end % w(), endy = mSelState.end / w();

	u32 index = 0;
	for (u16 y = starty; y <= endy; y++) {
		u16 x = (y == starty ? startx : 0);
		u16 end = (y == endy ? endx : (w() -1));
		for (; x <= end; x++) {
			buf[index++] = charCode(x, y);
			if (charAttr(x, y).type == CharAttr::DoubleLeft) x++;
		}
	}

	utf16_to_utf8(buf, index, text);
	mSelText.setText(text);
}

void Shell::resetTextSelect()
{
	mSelState.selecting = false;
	if (mSelState.color_inversed) {
		mSelState.color_inversed = false;
		changeTextColor(mSelState.start, mSelState.end, false);
	}
}

void Shell::autoTextSelect(u16 x, u16 y)
{
	mSelState.selecting = true;

	static const u32 inwordLut[8] = {
		0x00000000, /* control chars     */
		0x03FF6000, /* digits and '-' and '.' */
		0x87FFFFFE, /* uppercase and '_' */
		0x07FFFFFE, /* lowercase         */
		0x00000000,
		0x00000000,
		0xFF7FFFFF, /* latin-1 accented letters, not multiplication sign */
		0xFF7FFFFF  /* latin-1 accented letters, not division sign */
	};

	#define inword(c) ((c) > 0xff || (( inwordLut[(c) >> 5] >> ((c) & 0x1F) ) & 1))
	#define isspace(c)	((c) == ' ')

	u16 code = charCode(x, y);
	bool spc = isspace(code);

	u16 startx = x;
	while (1) {
		if ((spc && !isspace(code)) || (!spc && !inword(code))) {
			startx++;
			break;
		}

		if (!startx) break;

		startx--;
		code = charCode(startx, y);
	}


	code = charCode(x, y);
	spc = isspace(code);

	u16 endx = x;
	while (1) {
		if ((spc && !isspace(code)) || (!spc && !inword(code))) {
			endx--;
			break;
		}

		if (endx == w() - 1) break;

		endx++;
		code = charCode(endx, y);
	}

	if (charAttr(startx, y).type == CharAttr::DoubleRight) startx--;
	if (charAttr(endx, y).type == CharAttr::DoubleLeft) endx++;

	mSelState.start = y * w() + startx;
	mSelState.end = y * w() + endx;
	mSelState.color_inversed = true;
	changeTextColor(mSelState.start, mSelState.end, true);
}

void Shell::putSelectedText()
{
	if (mSelText.text) {
		sendBack(mSelText.text);
	}
}

void Shell::changeTextColor(u32 start, u32 end, bool inverse)
{
	u16 startx, starty, endx, endy;
	startx = start % w(), starty = start / w();
	endx = end % w(), endy = end / w();

	if (charAttr(startx, starty).type == CharAttr::DoubleRight) startx--;
	if (charAttr(endx, endy).type == CharAttr::DoubleLeft) endx++;

	mInverseText = inverse;
	requestUpdate(startx, starty, (starty == endy ? (endx + 1) : w()) - startx, 1);
	
	if (endy > starty + 1) {
		requestUpdate(0, starty + 1, w(), endy - starty - 1);
	}
	
	if (endy > starty) {
		requestUpdate(0, endy, endx + 1, 1);
	}

	mInverseText = false;
}

void Shell::drawCursor(CharAttr attr, u16 x, u16 y, u16 c)
{
	adjustCharAttr(attr);
	mCursor.attr = attr;
	mCursor.x = x;
	mCursor.y = y;
	mCursor.showed = false;

	updateCursor();
}

void Shell::updateCursor()
{
	if (mCursor.x >= w() || mCursor.y >= h()) return;

	mCursor.showed ^= true;
	drawCursor(mCursor.x, mCursor.y, mCursor.showed ? mCursor.attr.fcolor : mCursor.attr.bcolor);
}

void Shell::modeChange(ModeType type)
{
	bool val = mode(type);
	
	if (type == CursorVisible) {
		enableCursor(val);
	}
}

void Shell::adjustCharAttr(CharAttr &attr)
{
	if (attr.italic) attr.fcolor = 2; // green
	else if (attr.underline) attr.fcolor = 6; // cyan
	else if (attr.intensity == 0) attr.fcolor = 8; // gray

	if (attr.fcolor == -1) attr.fcolor = default_fcolor;
	if (attr.bcolor == -1) attr.bcolor = default_bcolor;

	if (attr.blink) attr.bcolor ^= 8;
	if (attr.intensity == 2) attr.fcolor ^= 8;

	if (attr.reverse ^ mInverseText) {
		s32 temp = attr.bcolor;
		attr.bcolor = attr.fcolor;
		attr.fcolor = temp;
		
		if (attr.bcolor > 8) attr.bcolor -= 8;
	}
}

void Shell::enterLeave(bool enter, Shell *pair)
{
    enableCursor(enter ? mode(CursorVisible) : false);

	if (!enter) {
		clearMousePointer();
		resetTextSelect();
	}
}

void Shell::clearMousePointer()
{
#ifdef DRAW_MOUSE_POINTER
	if (!mMousePointer.drawed) return;
	mMousePointer.drawed = false;

	if (mMousePointer.color_inversed) {
		changeTextColor(mMousePointer.pos, mMousePointer.pos, false);
	}
#endif
}

void Shell::drawMousePointer(u16 x, u16 y)
{
#ifdef DRAW_MOUSE_POINTER
	if (charAttr(x,y).type == CharAttr::DoubleRight) x--;

	u32 pos = y * w() + x;
	if (mMousePointer.drawed && pos == mMousePointer.pos) return;

	if (mMousePointer.drawed) {
		changeTextColor(mMousePointer.pos, mMousePointer.pos, !mMousePointer.color_inversed);
	}

	mMousePointer.drawed = true;
	mMousePointer.pos = pos;
	mMousePointer.color_inversed = !(mSelState.color_inversed && pos >= mSelState.start && pos <= mSelState.end);

	changeTextColor(pos, pos, mMousePointer.color_inversed);
#endif
}
