/*
 *   Copyright © 2008-2009 dragchan <zgchan317@gmail.com>
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

#ifndef IM_PROXY_H
#define IM_PROXY_H

#include "type.h"
#include "io.h"
#include "immessage.h"

class FbShell;

typedef enum { Intersect, Inside, Outside } IntersectState;

class ImProxy: public IoPipe {
public:
	ImProxy(FbShell *shell);
	~ImProxy();

	s32 imProcessId() { return mPid; }
	bool actived();
	void toggleActive();
	void sendKey(s8 *buf, u32 len);
	void changeCursorPos(u16 col, u16 row);
	void changeTermMode(bool crlf, bool appkey, bool curo);
	void switchVt(bool enter, ImProxy *peer);

	IntersectState intersectWithImWin(const Rectangle &rect);
	void redrawImWin(const Rectangle &rect);

private:
	virtual void readyRead(s8 *buf, u32 len);
	virtual void ioError(bool read, s32 err);

	void createImProcess();
	void waitImMessage(u32 type);
	void sendInfo();
	void sendAckWin();
	void sendAckPing();
	void sendDisconnect();
	void setImWin(u32 winid, Rectangle &rect);

	FbShell *mShell;
	s32 mPid;
	bool mConnected;
	bool mActive;
	bool mRawInput;
	enum {NoMessageToWait, WaitingMessage, GotMessage} mMsgWaitState;
	u32 mMsgWaitType;
	Rectangle mWins[NR_IM_WINS];
	u32 mValidWinNum;
};

#endif
