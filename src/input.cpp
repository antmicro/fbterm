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
#include "fbterm.h"

static termios oldTm;
static long oldKbMode;

DEFINE_INSTANCE(TtyInput)

TtyInput *TtyInput::createInstance()
{
	s8 buf[64];
	if (ttyname_r(STDIN_FILENO, buf, sizeof(buf))) {
		printf("stdin isn't a tty!\n");
		return 0;
	}

	if (!strstr(buf, "/dev/tty") && !strstr(buf, "/dev/vc")) {
		printf("stdin isn't a interactive tty!\n");
		return 0;
	}

	return new TtyInput();
}

TtyInput::TtyInput()
{
	ioctl(STDIN_FILENO, KDGKBMODE, &oldKbMode);
	switchVc(true);

	tcgetattr(STDIN_FILENO, &oldTm);

	termios tm = oldTm;
	
	cfmakeraw(&tm);
	tm.c_cc[VMIN] = 1;
	tm.c_cc[VTIME] = 0;	

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &tm);

	setFd(dup(STDIN_FILENO));
}

TtyInput::~TtyInput()
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldTm);
	switchVc(false);
}

void TtyInput::switchVc(bool enter)
{
	ioctl(STDIN_FILENO, KDSKBMODE, enter ? K_UNICODE : oldKbMode);
	setupSysKey(!enter);
}

void TtyInput::setupSysKey(bool restore)
{
	#define T_SHIFT (1 << KG_SHIFT)
	#define T_CTRL_ALT ((1 << KG_CTRL) + (1 << KG_ALT))

	static bool syskey_saved = false;
	static struct KeyEntry {
		u8 table;
		u8 keycode;
		u16 new_val;
		u16 old_val;
	} sysKeyTable[] = {
		{T_SHIFT, KEY_PAGEUP,	SHIFT_PAGEUP},
		{T_SHIFT, KEY_PAGEDOWN,	SHIFT_PAGEDOWN},
		{T_SHIFT, KEY_LEFT,		SHIFT_LEFT},
		{T_SHIFT, KEY_RIGHT,	SHIFT_RIGHT},
		{T_CTRL_ALT, KEY_1, CTRL_ALT_1},
		{T_CTRL_ALT, KEY_2, CTRL_ALT_2},
		{T_CTRL_ALT, KEY_3, CTRL_ALT_3},
		{T_CTRL_ALT, KEY_4, CTRL_ALT_4},
		{T_CTRL_ALT, KEY_5, CTRL_ALT_5},
		{T_CTRL_ALT, KEY_6, CTRL_ALT_6},
		{T_CTRL_ALT, KEY_7, CTRL_ALT_7},
		{T_CTRL_ALT, KEY_8,	CTRL_ALT_8},
		{T_CTRL_ALT, KEY_9, CTRL_ALT_9},
		{T_CTRL_ALT, KEY_0, CTRL_ALT_0},
		{T_CTRL_ALT, KEY_C, CTRL_ALT_C},
		{T_CTRL_ALT, KEY_D, CTRL_ALT_D},
		{T_CTRL_ALT, KEY_E, CTRL_ALT_E},
		{T_CTRL_ALT, KEY_F1, CTRL_ALT_F1},
		{T_CTRL_ALT, KEY_F2, CTRL_ALT_F2},
		{T_CTRL_ALT, KEY_F3, CTRL_ALT_F3},
		{T_CTRL_ALT, KEY_F4, CTRL_ALT_F4},
		{T_CTRL_ALT, KEY_F5, CTRL_ALT_F5},
		{T_CTRL_ALT, KEY_F6, CTRL_ALT_F6},
	};

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

	if (!syskey_saved) syskey_saved = true;

	seteuid(getuid());
}

void TtyInput::readyRead(s8 *buf, u32 len)
{
	static const u8 sys_utf8_start = 0xc0 | (AC_START >> 6);

	FbShell *shell = FbShellManager::instance()->activeShell();
	u32 i, start = 0;
	for (i = 0; i < len; i++) {
		u32 orig = i;
		u8 c = buf[i];

		if (c == sys_utf8_start && i < (len - 1)) c = (buf[++i] & 0x3f) | (AC_START & 0xc0);
		if (c < AC_START || c > AC_END) continue;

		if (orig > start && shell) shell->keyInput(buf + start, orig - start);
		start = i + 1;

		FbTerm::instance()->processSysKey(c);
	}

	if (i > start && shell) shell->keyInput(buf + start, i - start);
}
