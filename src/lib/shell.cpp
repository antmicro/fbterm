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
		default_fcolor = initDefaultColor(true);
		default_bcolor = initDefaultColor(false);
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
	u16 rtype = mode(MouseReport);
	
	bool selecting = (rtype == MouseNone || (modifies & ShiftButton));
	
	if (type == Move && (!selecting || btn == NoButton)) {
		drawMousePointer(x, y);
	}

	if (selecting) {
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
		if (rtype == MouseX11) val = 3;
		break;
	default:
		break;
	}

	if (rtype == MouseX11 && val != -1) {
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
	if (btn == LeftButton) {
		switch (type) {
		case Press:
			resetTextSelect();
			mSelState.selecting = true;
			startTextSelect(x, y);
			break;
		case Move:
			if (mSelState.selecting) {
				middleTextSelect(x, y);
			} else {
				resetTextSelect();
				mSelState.selecting = true;
				startTextSelect(x, y);
			}
			break;
		case Release:
			mSelState.selecting = false;
			endTextSelect();
			break;
		case DblClick:
			resetTextSelect();
			autoTextSelect(x, y);
			break;
		default:
			break;
		}
	} else if (btn == RightButton) {
		if (type == Press || type == DblClick) {
			resetTextSelect();
			putSelectedText();
		}
	}
}

void Shell::startTextSelect(u16 x, u16 y)
{
	mSelState.start = mSelState.end = y * w() + x;

	changeTextColor(mSelState.start, mSelState.end, true);
	mSelState.color_inversed = true;
}

void Shell::middleTextSelect(u16 x, u16 y)
{
	u32 start = mSelState.start, end = mSelState.end;
	u32 new_end = y * w() + x;
	
	bool dir_sel = (end >= start);
	bool dir_new_sel = (new_end >= start);
	
	mSelState.end = new_end;
	
	if (dir_sel == dir_new_sel) {
		bool dir_change = (new_end > end);
		
		u32 &pos = (dir_sel == dir_change) ? end : new_end;
		CharAttr attr = charAttr(pos % w(), pos / w());
		
		if (dir_sel) {
			if (attr.type == CharAttr::DoubleLeft) pos++;
			pos++;
		} else {
			if (attr.type == CharAttr::DoubleRight) pos--;
			pos--;
		}
		
		bool dir_new_change = (new_end == end) ? dir_change : (new_end > end);
		
		if (dir_change == dir_new_change) {
			changeTextColor(end, new_end, dir_sel == dir_change);
		}
	} else {
		changeTextColor(start, end, false);
		changeTextColor(start, new_end, true);
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

#define SWAP(a, b) do \
	if (a > b) { \
		u32 tmp = a; \
		a = b; \
		b = tmp; \
	} \
while (0)

void Shell::endTextSelect()
{
	mMousePointer.drawed = false;

	u32 start = mSelState.start, end = mSelState.end;
	SWAP(start, end);
	
	u32 len = end - start + 1;
	u16 buf[len];
	s8 *text = new s8[len * 3];

	u16 startx, starty, endx, endy;
	startx = start % w(), starty = start / w();
	endx = end % w(), endy = end / w();

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
	static u32 inwordLut[8] = {
		0x00000000,
		0x03FF0000, /* digits */
		0x07FFFFFE, /* uppercase */
		0x07FFFFFE, /* lowercase */
		0x00000000,
		0x00000000,
		0xFF7FFFFF, /* latin-1 accented letters, not multiplication sign */
		0xFF7FFFFF  /* latin-1 accented letters, not division sign */
	};
	
	static bool inited = false;
	if (!inited) {
		inited = true;
		
		u8 chrs[32];
		initWordChars((s8*)chrs, sizeof(chrs));
		
		for (u32 i = 0; chrs[i]; i++) {
			if (chrs[i] > 0x7f || chrs[i] <= ' ') continue;
			inwordLut[chrs[i] >> 5] |= 1 << (chrs[i] & 0x1f);
		}
	}

	#define inword(c) ((c) > 0xff || (( inwordLut[(c) >> 5] >> ((c) & 0x1f) ) & 1))
	#define isspace(c)	((c) == ' ')

	u16 code = charCode(x, y);
	bool spc = isspace(code);

	u16 startx = x;
	while (1) {
		if (!startx || (spc && !isspace(code)) || (!spc && !inword(code))) break;
		startx--;
		code = charCode(startx, y);
	}

	if (startx < x) startx++;

	code = charCode(x, y);
	spc = isspace(code);

	u16 endx = x;
	while (1) {
		if (endx == w() -1 || (spc && !isspace(code)) || (!spc && !inword(code))) break;
		endx++;
		code = charCode(endx, y);
	}

	if (endx > x) endx--;

	mSelState.start = y * w() + startx;
	mSelState.end = y * w() + endx;
	changeTextColor(mSelState.start, mSelState.end, true);
	mSelState.color_inversed = true;
}

void Shell::putSelectedText()
{
	if (mSelText.text) {
		sendBack(mSelText.text);
	}
}

void Shell::changeTextColor(u32 start, u32 end, bool inverse)
{
	SWAP(start, end);

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

void Shell::switchVt(bool enter)
{
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
	
	u32 start = mSelState.start, end = mSelState.end;
	SWAP(start, end);
	if (charAttr(start % w(), start / w()).type == CharAttr::DoubleRight) start--;
	
	mMousePointer.color_inversed = !(mSelState.color_inversed && pos >= start && pos <= end);

	changeTextColor(pos, pos, mMousePointer.color_inversed);
#endif
}
