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
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/vt.h>
#include <linux/kd.h>
#include <linux/input.h>
#include "input.h"
#include "input_key.h"
#include "fbshell.h"
#include "fbshellman.h"
#include "fbterm.h"
#include "improxy.h"

static termios oldTm;
static long oldKbMode;

DEFINE_INSTANCE(TtyInput)

TtyInput *TtyInput::createInstance()
{
	s8 buf[64];
	if (ttyname_r(STDIN_FILENO, buf, sizeof(buf))) {
		fprintf(stderr, "stdin isn't a tty!\n");
		return 0;
	}

	if (!strstr(buf, "/dev/tty") && !strstr(buf, "/dev/vc")) {
		fprintf(stderr, "stdin isn't a interactive tty!\n");
		return 0;
	}

	return new TtyInput();
}

TtyInput::TtyInput()
{
	tcgetattr(STDIN_FILENO, &oldTm);

	termios tm = oldTm;
	cfmakeraw(&tm);
	tm.c_cc[VMIN] = 1;
	tm.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &tm);

	ioctl(STDIN_FILENO, KDGKBMODE, &oldKbMode);
	switchIm(false, false);

	setFd(dup(STDIN_FILENO));
}

TtyInput::~TtyInput()
{
	setupSysKey(true);
	ioctl(STDIN_FILENO, KDSKBMODE, oldKbMode);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldTm);
}

void TtyInput::switchVc(bool enter)
{
	setupSysKey(!enter);
}

void TtyInput::setupSysKey(bool restore)
{
	#define T_SHIFT (1 << KG_SHIFT)
	#define T_CTRL (1 << KG_CTRL)
	#define T_CTRL_ALT ((1 << KG_CTRL) + (1 << KG_ALT))

	static bool syskey_saved = false;
	static struct KeyEntry {
		u8 table;
		u8 keycode;
		u16 new_val;
		u16 old_val;
	} sysKeyTable[] = {
		{T_SHIFT,    KEY_PAGEUP,   SHIFT_PAGEUP},
		{T_SHIFT,    KEY_PAGEDOWN, SHIFT_PAGEDOWN},
		{T_SHIFT,    KEY_LEFT,	   SHIFT_LEFT},
		{T_SHIFT,    KEY_RIGHT,    SHIFT_RIGHT},
		{T_CTRL,     KEY_SPACE,    CTRL_SPACE},
		{T_CTRL_ALT, KEY_1,        CTRL_ALT_1},
		{T_CTRL_ALT, KEY_2,        CTRL_ALT_2},
		{T_CTRL_ALT, KEY_3,        CTRL_ALT_3},
		{T_CTRL_ALT, KEY_4,        CTRL_ALT_4},
		{T_CTRL_ALT, KEY_5,        CTRL_ALT_5},
		{T_CTRL_ALT, KEY_6,        CTRL_ALT_6},
		{T_CTRL_ALT, KEY_7,        CTRL_ALT_7},
		{T_CTRL_ALT, KEY_8,	       CTRL_ALT_8},
		{T_CTRL_ALT, KEY_9,        CTRL_ALT_9},
		{T_CTRL_ALT, KEY_0,        CTRL_ALT_0},
		{T_CTRL_ALT, KEY_C,        CTRL_ALT_C},
		{T_CTRL_ALT, KEY_D,        CTRL_ALT_D},
		{T_CTRL_ALT, KEY_E,        CTRL_ALT_E},
		{T_CTRL_ALT, KEY_F1,       CTRL_ALT_F1},
		{T_CTRL_ALT, KEY_F2,       CTRL_ALT_F2},
		{T_CTRL_ALT, KEY_F3,       CTRL_ALT_F3},
		{T_CTRL_ALT, KEY_F4,       CTRL_ALT_F4},
		{T_CTRL_ALT, KEY_F5,       CTRL_ALT_F5},
		{T_CTRL_ALT, KEY_F6,       CTRL_ALT_F6},
	};

	if (!syskey_saved && restore) return;

	extern s32 effective_uid;
	seteuid(effective_uid);

	for (u32 i = 0; i < sizeof(sysKeyTable) / sizeof(KeyEntry); i++) {
		kbentry entry;
		entry.kb_table = sysKeyTable[i].table;
		entry.kb_index = sysKeyTable[i].keycode;

		if (!syskey_saved) {
			ioctl(STDIN_FILENO, KDGKBENT, &entry);
			sysKeyTable[i].old_val = entry.kb_value;
		}

		entry.kb_value = (restore ? sysKeyTable[i].old_val : sysKeyTable[i].new_val);
		ioctl(STDIN_FILENO, KDSKBENT, &entry); //should have perm CAP_SYS_TTY_CONFIG
	}

	if (!syskey_saved && !restore) syskey_saved = true;

	seteuid(getuid());
}

void TtyInput::readyRead(s8 *buf, u32 len)
{
	if (mRawMode) {
		processRawKeys(buf, len);
		return;
	}

	#define PUT_KEYS(buf, len) \
	do { \
		if (mImEnable) { \
			ImProxy::instance()->sendKey(buf, len); \
		} else { \
			FbShell *shell = FbShellManager::instance()->activeShell(); \
			if (shell) { \
				shell->keyInput(buf, len); \
			} \
		} \
	} while (0)

	u32 start = 0;
	for (u32 i = 0; i < len; i++) {
		u32 orig = i;
		u16 c = (u8)buf[i];

		if ((c >> 5) == 0x6 && i < (len - 1) && (((u8)buf[++i]) >> 6) == 0x2) {
			c = ((c & 0x1f) << 6) | (buf[i] & 0x3f);
			if (c < AC_START || c > AC_END) continue;

			if (orig > start) PUT_KEYS(buf + start, orig - start);
			start = i + 1;

			FbTerm::instance()->processSysKey(c);
		}
	}

	if (len > start) PUT_KEYS(buf + start, len - start);
}

typedef enum {
	ALT_L = 1 << 0, ALT_R = 1 << 1,
	CTRL_L = 1 << 2, CTRL_R = 1 << 3,
	SHIFT_L = 1 << 4, SHIFT_R = 1 << 5,
} ModifierType;

static u16 modState;

void TtyInput::switchIm(bool enter, bool raw)
{
	modState = 0;
	mImEnable = enter;
	mRawMode = (enter && raw);

	ioctl(STDIN_FILENO, KDSKBMODE, mRawMode ? K_MEDIUMRAW : K_UNICODE);
	setupSysKey(mRawMode);
}

void TtyInput::processRawKeys(s8 *buf, u32 len)
{
	for (u32 i = 0; i < len; i++) {
		bool down = !(buf[i] & 0x80);
		u16 code = buf[i] & 0x7f;

		if (!code) {
			if (i + 2 >= len) break;

			code = (buf[++i] & 0x7f) << 7;
			code |= buf[++i] & 0x7f;
			if (!(buf[i] & 0x80) || !(buf[i - 1] & 0x80)) continue;
		}

		u16 mod = 0;
		switch (code) {
		case KEY_LEFTALT:
			mod = ALT_L;
			break;

		case KEY_RIGHTALT:
			mod = ALT_R;
			break;

		case KEY_LEFTCTRL:
			mod = CTRL_L;
			break;

		case KEY_RIGHTCTRL:
			mod = CTRL_R;
			break;

		case KEY_LEFTSHIFT:
			mod = SHIFT_L;
			break;

		case KEY_RIGHTSHIFT:
			mod = SHIFT_R;
			break;

		default:
			break;
		}

		if (mod) {
			if (down) modState |= mod;
			else modState &= ~mod;
		} else if (down) {
			u16 ctrl = (CTRL_L | CTRL_R);
			if ((modState & ctrl) && !(modState & ~ctrl) && code == KEY_SPACE) {
				FbTerm::instance()->processSysKey(CTRL_SPACE);
				return;
			}
		}
	}

	ImProxy::instance()->sendKey(buf, len);
}
