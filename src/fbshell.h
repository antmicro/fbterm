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

#ifndef FBSHELL_H
#define FBSHELL_H

#include "instance.h"
#include "shell.h"

#define NR_COLORS 16

struct Color {
	u8 blue, green, red;
};
	
class FbShell : public Shell {
public:
	void switchCodec(u8 index);

private:
	friend class FbShellManager;
	FbShell();
	~FbShell();

	virtual void drawChars(CharAttr attr, u16 x, u16 y, u16 num, u16 *chars, bool *dws);
	virtual bool moveChars(u16 sx, u16 sy, u16 dx, u16 dy, u16 w, u16 h);
	virtual void drawCursor(CharAttr attr, u16 x, u16 y, u16 c);
	virtual void modeChanged(ModeType type);
	virtual void request(RequestType type, u32 val = 0);

	virtual void initChildProcess();
	virtual void switchVt(bool enter);

	void enableCursor(bool enable);
	void updateCursor();

	struct Cursor {
		Cursor() {
			x = y = (u16)-1;
			showed = false;
		}
		bool showed;
		u16 x, y;
		u16 code;
		CharAttr attr;
	} mCursor;

	bool mPaletteChanged;
	Color mPalette[NR_COLORS];
};

class FbShellManager {
	DECLARE_INSTANCE(FbShellManager)
public:
	FbShell *activeShell() {
		return mActiveShell;
	}
	void createShell();
	void deleteShell();
	void shellExited(FbShell *shell);
	void switchShell(u32 num);
	void nextShell();
	void prevShell();
	void drawCursor();
	void historyScroll(bool down);
	void redraw(u16 x, u16 y, u16 w, u16 h);
	void switchVc(bool enter);

private:
	u32 getIndex(FbShell *shell, bool forward, bool stepfirst);
	void setActive(FbShell *shell);

	#define NR_SHELLS 10
	FbShell *mShellList[NR_SHELLS], *mActiveShell;
	u32 mShellCount, mCurShell;
	bool mVcCurrent;
};
#endif
