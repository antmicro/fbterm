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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "imapi.h"
#include "keycode.h"
#include "font.h"
#include "screen.h"

static char raw_mode = 1;
static char first_show = 1;

static void im_active()
{
	if (raw_mode) {
		init_keycode_state();
	}
}

static void im_deactive()
{
	set_im_windows(0, 0);
	first_show = 1;
}

static void process_raw_key(char *buf, unsigned short len) 
{
	for (unsigned short i = 0; i < len; i++) {
		char down = !(buf[i] & 0x80);
		short code = buf[i] & 0x7f;

		if (!code) {
			if (i + 2 >= len) break;

			code = (buf[++i] & 0x7f) << 7;
			code |= buf[++i] & 0x7f;
			if (!(buf[i] & 0x80) || !(buf[i - 1] & 0x80)) continue;
		}
        
		unsigned short keysym = keycode_to_keysym(code, down);
		char *str = keysym_to_term_string(keysym, down);

		put_im_text(str, strlen(str));
	}
}

static void process_key(char *keys, unsigned short len)
{
	if (raw_mode) {
		process_raw_key(keys, len);
	} else {
		put_im_text(keys, len);
	}
}

static void cursor_pos_changed(unsigned x, unsigned y)
{
	static const char str[] = "a IM example";
	#define NSTR (sizeof(str) - 1)
	
	ImWin wins[] = {
		{ x + 10, y + 10, 40, 20 },
		{ x + 10, y + 40, W(NSTR) + 10, H(1) + 10 }
	};
	set_im_windows(wins, 2);
	
	if (first_show) {
		first_show = 0;

		// should call this function once when IM begins to reshow UI after hiding UI last time
		Screen::instance()->updateYOffset();
	}

	Screen::instance()->fillRect(wins[0].x, wins[0].y, wins[0].w, wins[0].h, White);

	// the better way is only filling margins
	Screen::instance()->fillRect(wins[1].x, wins[1].y, wins[1].w, wins[1].h, White);
	
	unsigned short unistr[NSTR];
	bool dws[NSTR];
	for (int i = 0; i < NSTR; i++) {
		unistr[i] = str[i];
		dws[i] = is_double_width(str[i]);
	}

	Screen::instance()->drawText(wins[1].x + 5, wins[1].y + 5, Black, White, NSTR, unistr, dws);
}

static void update_fbterm_info(Info *info)
{
	Font::setFontInfo(info->fontName, info->fontSize);
	Screen::setRotateType((RotateType)info->rotate);
	if (!Screen::instance()) {
		exit(1);
	}
}

static ImCallbacks cbs = {
	im_active, // .active
	im_deactive, // .deactive
	process_key, // .send_key
	cursor_pos_changed, // .cursor_position
	update_fbterm_info, // .fbterm_info
	update_term_mode // .term_mode
};

int main()
{
	register_im_callbacks(cbs);
	connect_fbterm(raw_mode);
	while (check_im_message()) ;

	Screen::uninstance();
	return 0;
}
