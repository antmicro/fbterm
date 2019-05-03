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

#ifndef SHELL_H
#define SHELL_H

#include "io.h"
#include "vterm.h"

enum MouseType { Press = 0, Release, DblClick, Move };

enum ButtonState {
	NoButton	= 0x0000,
	LeftButton	= 0x0001,
	RightButton	= 0x0002,
	MidButton	= 0x0004,
	MouseButtonMask = 0x0007,

	ShiftButton	= 0x0100,
	ControlButton	= 0x0200,
	AltButton	= 0x0400,
	ModifyButtonMask	= 0x0700
};

class Shell : public IoPipe, public VTerm {
public:
	void keyInput(s8 *buf, u32 len);
	void mouseInput(u16 x, u16 y, s32 type, s32 buttons);

protected:
	Shell();
	~Shell();

	void resize(u16 w, u16 h);
	void createChildProcess();
	virtual void initChildProcess() {}
	virtual void adjustCharAttr(CharAttr &attr);
	virtual void switchVt(bool enter);
	u8 defaultColor(bool foreground) { return foreground ? default_fcolor : default_bcolor; }

private:
	virtual void readyRead(s8 *buf, u32 len);
	virtual void sendBack(const s8 *data);

	void changeTextColor(u32 start, u32 end, bool inverse);
	void textSelect(u16 x, u16 y, s32 type, s32 buttons);
	void startTextSelect(u16 x, u16 y);
	void middleTextSelect(u16 x, u16 y);
	void endTextSelect();
	void resetTextSelect();
	void autoTextSelect(u16 x, u16 y);
	void putSelectedText();

	void drawMousePointer(u16 x, u16 y);
	void clearMousePointer();

	static u8 initDefaultColor(bool foreground);
	static void initWordChars(s8 *buf, u32 len);

	static u8 default_fcolor, default_bcolor;

	s32 mPid;
	bool mInverseText;

	static struct SelectedText {
		SelectedText() {
			text = 0;
		}
		~SelectedText() {
			if (text) delete[] text;
		}
		void setText(s8 *_text) {
			if (text) delete[] text;
			text = _text;
		}

		s8 *text;
	} mSelText;
	
	struct TextSelection {
		TextSelection() {
			selecting = color_inversed = false;
		}
		u32 start, end;
		bool selecting;
		bool positive_direction;
		bool color_inversed;
	} mSelState;

	struct MousePointer {
		MousePointer() {
			drawed = false;
		}
		u32 pos;
		bool color_inversed;
		bool drawed;
	} mMousePointer;
};

#endif
