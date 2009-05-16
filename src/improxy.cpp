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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include "improxy.h"
#include "immessage.h"
#include "fbconfig.h"
#include "fbshell.h"
#include "fbshellman.h"
#include "font.h"
#include "screen.h"
#include "input.h"
#include "fbterm.h"

#define OFFSET(TYPE, MEMBER) ((size_t)(&(((TYPE *)0)->MEMBER)))
#define MSG(a) ((Message *)(a))

ImProxy::ImProxy(FbShell *shell)
{
	mShell = shell;
	mPid = -1;
	mConnected = false;
	mActive = false;
	mMsgWaitState = NoMessageToWait;

	createImProcess();
}

ImProxy::~ImProxy()
{
	mShell->ImExited();
	if (!mConnected) return;

	if (FbShellManager::instance()->activeShell() == mShell) {
		Screen::instance()->setClipRects(0, 0);
		TtyInput::instance()->setRawMode(false);
	}

	sendDisconnect();
	setFd(-1);

	extern void waitChildProcessExit(s32 pid);
	waitChildProcessExit(mPid);
}

void ImProxy::createImProcess()
{
	s8 app[128];
	Config::instance()->getOption("input-method", app, sizeof(app));
	if (!app[0]) return;

	int fds[2];
	if (socketpair(AF_LOCAL, SOCK_STREAM, 0, fds) == -1) return;

	mPid = fork();

	switch (mPid) {
	case -1:
		close(fds[0]);
		close(fds[1]);
		break;

	case 0: {
		FbTerm::instance()->initChildProcess();
		close(fds[0]);

		s8 buf[16];
		snprintf(buf, sizeof(buf), "%d", fds[1]);
		setenv("FBTERM_IM_SOCKET", buf, 1);

		execlp(app, app, NULL);
		fprintf(stderr, "can't execute IM program %s!\n", app);
		exit(1);
		break;
	}

	default:
		close(fds[1]);
		setFd(fds[0]);
		waitImMessage(Connect);
		break;
	}
}

bool ImProxy::actived()
{
	return mActive;
}

void ImProxy::toggleActive()
{
	if (!mConnected) return;

	TtyInput::instance()->setRawMode(mRawInput && !mActive);
	mActive ^= true;

	Message msg;
	msg.type = (mActive ? Active : Deactive);
	msg.len = sizeof(msg);
	write((s8 *)&msg, sizeof(msg));
}

void ImProxy::changeCursorPos(u16 col, u16 row)
{
	if (!mConnected || !mActive) return;

	Message msg;
	msg.type = CursorPosition;
	msg.len = sizeof(msg);
	msg.cursor.x = FW(col);
	msg.cursor.y = FH(row + 1);

	write((s8 *)&msg, sizeof(msg));
}

void ImProxy::changeTermMode(bool crlf, bool appkey, bool curo)
{
	if (!mConnected || !mActive) return;

	Message msg;
	msg.type = TermMode;
	msg.len = sizeof(msg);
	msg.term.crWithLf = crlf;
	msg.term.applicKeypad = appkey;
	msg.term.cursorEscO = curo;

	write((s8 *)&msg, sizeof(msg));
}

void ImProxy::switchVt(bool enter, ImProxy *peer)
{
	if (!mConnected || !mActive) return;

	TtyInput::instance()->setRawMode(enter && mRawInput);

	Message msg;
	msg.type = (enter ? ShowUI : HideUI);
	msg.len = sizeof(msg);

	write((s8 *)&msg, sizeof(msg));

	if (!enter) {
		waitImMessage(AckHideUI);
		Screen::instance()->setClipRects(0, 0);
	}
}

void ImProxy::sendKey(s8 *keys, u32 len)
{
	if (!mConnected || !mActive || !keys || !len) return;

	s8 buf[OFFSET(Message, keys) + len];

	MSG(buf)->type = SendKey;
	MSG(buf)->len = sizeof(buf);
	memcpy(MSG(buf)->keys, keys, len);

	write(buf, MSG(buf)->len);
}

void ImProxy::sendInfo()
{
	u32 pixel_size = 12;
	Config::instance()->getOption("font-size", pixel_size);

	s8 name[64];
	Config::instance()->getOption("font-names", name, sizeof(name));
	if (!*name) {
		memcpy(name, "mono", 5);
	}

	u32 len = strlen(name) + 1;
	char buf[OFFSET(Message, info.fontName) + len];

	MSG(buf)->type = FbTermInfo;
	MSG(buf)->len = sizeof(buf);
	MSG(buf)->info.rotate = Screen::instance()->rotateType();
	MSG(buf)->info.fontSize = pixel_size;
	MSG(buf)->info.fontHeight = FH(1);
	MSG(buf)->info.fontWidth = FW(1);
	memcpy(MSG(buf)->info.fontName, name, len);

	write(buf, MSG(buf)->len);
}

void ImProxy::sendAckWins()
{
	Message msg;
	msg.type = AckWins;
	msg.len = sizeof(msg);
	write((s8 *)&msg, sizeof(msg));
}

void ImProxy::sendDisconnect()
{
	if (!mConnected) return;

	Message msg;
	msg.type = Disconnect;
	msg.len = sizeof(msg);
	write((s8 *)&msg, sizeof(msg));
}

void ImProxy::readyRead(s8 *buf, u32 len)
{
	for (s8 *end = buf + len; buf < end && MSG(buf)->len && MSG(buf)->len <= (end - buf); buf += MSG(buf)->len) {
		if (mMsgWaitState == WaitingMessage && mMsgWaitType == MSG(buf)->type) mMsgWaitState = GotMessage;
		if (!mConnected && MSG(buf)->type != Connect) continue;

		switch (MSG(buf)->type) {
		case Connect:
			mConnected = true;
			mRawInput = MSG(buf)->raw;

			sendInfo();
			break;

		case PutText:
			if (MSG(buf)->len > OFFSET(Message, texts)) {
				mShell->imInput(MSG(buf)->texts, MSG(buf)->len - OFFSET(Message, texts));
			}
			break;

		case SetWins:
			sendAckWins();

			if (FbShellManager::instance()->activeShell() == mShell && MSG(buf)->len >= OFFSET(Message, wins)) {
				ImWin *wins = MSG(buf)->wins;
				u32 num = (MSG(buf)->len - OFFSET(Message, wins)) / sizeof(ImWin);

				Rectangle rects[num];
				for (u32 i = 0; i < num; i++) {
					rects[i].sx = wins[i].x;
					rects[i].sy = wins[i].y;
					rects[i].ex = wins[i].x + wins[i].w;
					rects[i].ey = wins[i].y + wins[i].h;
				}

				Screen::instance()->setClipRects(rects, num);
			}
			break;

		default:
			break;
		}
	}
}

void ImProxy::waitImMessage(u32 type)
{
	mMsgWaitState = WaitingMessage;
	mMsgWaitType = type;

	timeval tv = {2, 0};

	while (1) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(fd(), &fds);

		s32 ret = select(fd() + 1, &fds, 0, 0, &tv);

		if (ret > 0 && FD_ISSET(fd(), &fds)) ready(true);
		if ((ret == -1 && errno != EINTR) || !ret || mMsgWaitState == GotMessage) break;
	};

	mMsgWaitState = NoMessageToWait;
}

void ImProxy::ioError(bool read, s32 err)
{
	if (read && mMsgWaitState == WaitingMessage) mMsgWaitState = GotMessage;
}
