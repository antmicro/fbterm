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

#include "shell.h"

class FbShell : public Shell {
public:
	static FbShell *activeShell() {
		return mShellList[mCurShell];
	}
	static FbShell *createShell();
	static void deleteShell();
	static void switchShell(u8 num);
	static void nextShell();
	static void prevShell();
	static void enterLeaveVc(bool enter);
	static void drawCursor();
	static void redraw(u16 x, u16 y, u16 w, u16 h);
	void switchCodec(u8 index);

private:
	FbShell();
	~FbShell();

	virtual void drawChars(CharAttr attr, u16 x, u16 y, u16 num, u16 *chars, bool *dws);
	virtual bool moveChars(u16 sx, u16 sy, u16 dx, u16 dy, u16 w, u16 h);

	virtual void initChildProcess();
	virtual void enableCursor(bool enable);
	virtual void drawCursor(u16 x, u16 y, u8 color);
	static void setActive(FbShell *shell);

	static bool mVcCurrent;
	static u8 mShellCount, mCurShell;
	static FbShell *mShellList[], *mActiveShell;
};

#endif
