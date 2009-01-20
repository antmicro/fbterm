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
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include "improxy.h"
#include "immessage.h"
#include "fbconfig.h"
#include "fbshellman.h"
#include "font.h"
#include "screen.h"
#include "io.h"
#include "input.h"
#include "fbterm.h"

class ImSocket: public IoPipe {
public:
	ImSocket(s32 fd);
	~ImSocket();

	bool actived() { return mActive; }
	void toggleActive(u32 shell);
	void sendKey(s8 *buf, u32 len);
	void sendCursorPos(u16 x, u16 y);
	void sendTermMode(bool crlf, bool appkey, bool curo);

private:
	virtual void readyRead(s8 *buf, u32 len);

	void sendInfo();
	void sendAck();
	void sendDisconnect();

	bool mConnected;
	bool mActive;
	bool mInputMode;
};

#define OFFSET(TYPE, MEMBER) ((size_t)(&(((TYPE *)0)->MEMBER)))
#define MSG(a) ((Message *)(a))

DEFINE_INSTANCE_DEFAULT(ImProxy)

ImProxy::ImProxy()
{
	mSocket = 0;
	mPid = -1;

	createImProcess();
}

ImProxy::~ImProxy()
{
	if (mSocket) delete mSocket;
}

void ImProxy::socketEnd()
{
	mSocket = 0;

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
		mSocket = new ImSocket(fds[0]);
		break;
	}
}

bool ImProxy::actived()
{
	if (mSocket) return mSocket->actived();
	return false;
}

void ImProxy::toggleActive(u32 shell)
{
	if (mSocket) {
		mSocket->toggleActive(shell);
	}
}

void ImProxy::changeCursorPos(u16 col, u16 row)
{
	if (mSocket) {
		mSocket->sendCursorPos(col, row);
	}
}

void ImProxy::changeTermMode(bool crlf, bool appkey, bool curo)
{
	if (mSocket) {
		mSocket->sendTermMode(crlf, appkey, curo);
	}
}

void ImProxy::sendKey(s8 *buf, u32 len)
{
	if (mSocket) {
		mSocket->sendKey(buf, len);
	}
}


ImSocket::ImSocket(s32 fd)
{
	mConnected = false;
	mActive = false;
	setFd(fd);
}

ImSocket::~ImSocket()
{
	Screen::instance()->setClipRects(0, 0);
	if (mActive) {
		TtyInput::instance()->switchIm(false, false);
	}

	sendDisconnect();
	setFd(-1);
	ImProxy::instance()->socketEnd();
}

void ImSocket::toggleActive(u32 shell)
{
	if (!mConnected) return;

	mActive ^= true;
	TtyInput::instance()->switchIm(mActive, mInputMode);

	Message msg;
	msg.type = (mActive ? Active : Deactive);
	msg.len = sizeof(msg);
	msg.shell = shell;
	write((s8 *)&msg, sizeof(msg));
}

void ImSocket::sendKey(s8 *keys, u32 len)
{
	if (!mConnected || !mActive || !keys || !len) return;

	s8 buf[OFFSET(Message, texts.text) + len];

	MSG(buf)->type = SendKey;
	MSG(buf)->len = sizeof(buf);
	memcpy(MSG(buf)->texts.text, keys, len);

	write(buf, MSG(buf)->len);
}

void ImSocket::sendCursorPos(u16 col, u16 row)
{
	if (!mConnected || !mActive) return;

	Message msg;
	msg.type = CursorPosition;
	msg.len = sizeof(msg);
	msg.cursor.x = W(col);
	msg.cursor.y = H(row + 1);

	write((s8 *)&msg, sizeof(msg));
}

void ImSocket::sendTermMode(bool crlf, bool appkey, bool curo)
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

void ImSocket::sendInfo()
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
	MSG(buf)->info.rotate = Screen::rotateType();
	MSG(buf)->info.fontSize = pixel_size;
	memcpy(MSG(buf)->info.fontName, name, len);

	write(buf, MSG(buf)->len);
}

void ImSocket::sendAck()
{
	Message msg;
	msg.type = AckWins;
	msg.len = sizeof(msg);
	write((s8 *)&msg, sizeof(msg));
}

void ImSocket::sendDisconnect()
{
	if (!mConnected) return;

	Message msg;
	msg.type = Disconnect;
	msg.len = sizeof(msg);
	write((s8 *)&msg, sizeof(msg));	
}

void ImSocket::readyRead(s8 *buf, u32 len)
{
	for (s8 *end = buf + len; buf < end && MSG(buf)->len && MSG(buf)->len <= (end - buf); buf += MSG(buf)->len) {
		if (!mConnected && MSG(buf)->type != Connect) continue;
		
		switch (MSG(buf)->type) {
		case Connect:
			mConnected = true;
			mInputMode = MSG(buf)->raw;

			sendInfo();
			break;

		case PutText:
			if (MSG(buf)->len > OFFSET(Message, texts.text)) {
				FbShellManager::instance()->imInput(MSG(buf)->texts.shell, MSG(buf)->texts.text, 
					MSG(buf)->len - OFFSET(Message, texts.text));
			}
			break;

		case SetWins:
 			sendAck();

			if (MSG(buf)->len >= OFFSET(Message, wins)) {
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
