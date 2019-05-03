/*
 *   Copyright © 2008 dragchan <zgchan317@gmail.com>
 *   based on GTerm by Timothy Miller <tim@techsource.com>
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


#ifndef VTERM_H
#define VTERM_H

#include "type.h"

class VTerm {
public:
	u16 w() { return width; }
	u16 h() { return height; }
	void historyDisplay(bool absolute, s32 num);

protected:
	struct CharAttr {
		typedef enum { Single = 0, DoubleLeft, DoubleRight } CharType;

		bool operator != (const CharAttr a) {
			return fcolor != a.fcolor || bcolor != a.bcolor || intensity != a.intensity
				|| italic != a.italic || underline != a.underline || blink != a.blink || reverse != a.reverse;
		}

		s32 fcolor : 5;	// -1 = default
		s32 bcolor : 5;	// -1 = default
		u32 intensity : 2; // 0 = half-bright, 1 = normal, 2 = bold
		u32 italic : 1;
		u32 underline : 1;
		u32 blink : 1;
		u32 reverse : 1;
		CharType type : 2;
	};

	typedef enum { CursorVisible, CursorKeyEsc0, X10MouseReport, X11MouseReport, AutoRepeatKey, NumericKeypad } ModeType;

	VTerm(u16 w = 0, u16 h = 0);
	virtual ~VTerm();

	bool mode(ModeType type);
	void resize(u16 w, u16 h);
	void input(const u8 *buf, u32 count);
	void expose(u16 x, u16 y, u16 w, u16 h);

	u16 charCode(u16 x, u16 y) { return text[get_line(y) * max_width + x]; }
	CharAttr charAttr(u16 x, u16 y) { return attrs[get_line(y) * max_width + x]; }

	virtual void drawChars(CharAttr attr, u16 x, u16 y, u16 num, u16 *chars, bool *dws) = 0;
	virtual bool moveChars(u16 sx, u16 sy, u16 dx, u16 dy, u16 w, u16 h) { return false; }
	virtual void drawCursor(CharAttr attr, u16 x, u16 y, u16 c) {};
	virtual void sendBack(const s8 *data) {}
	virtual void modeChange(ModeType type) {}
	virtual void historyChange(u32 cur, u32 total) {}
	virtual void requestBell() {}
	virtual void requestSizeChange(u16 w, u16 h) {}
	virtual void requestUpdate(u16 x, u16 y, u16 w, u16 h);

private:
	// utility functions
	void do_normal_char();
	void do_control_char();
	void scroll_region(u16 start_y, u16 end_y, s16 num);	// does clear
	void shift_text(u16 y, u16 start_x, u16 end_x, s16 num); // ditto
	void clear_area(u16 start_x, u16 start_y, u16 end_x, u16 end_y);
	void changed_line(u16 y, u16 start_x, u16 end_x);
	void move_cursor(u16 x, u16 y);
	void update();
	void draw_cursor();
	u16 get_line(u16 y);
	u16 total_history_lines() { return history_full ? history_lines : history_save_line; }

	// terminal actions
	void set_q_mode();
	void clear_param();
	void param_digit();
	void next_param();

	// non-printing characters
	void cr(), lf(), bell(), tab(), bs();

	// escape sequence actions
	void reset();
	void keypad_numeric();
	void keypad_application();
	void save_cursor();
	void restore_cursor();
	void set_tab();
	void clear_tab();
	void index_down();
	void index_up();
	void next_line();
	void cursor_left();
	void cursor_right();
	void cursor_up();
	void cursor_down();
	void cursor_up_cr();
	void cursor_down_cr();
	void cursor_position();
	void cursor_position_col();
	void cursor_position_row();
	void insert_char();
	void delete_char();
	void erase_char();
	void insert_line();
	void delete_line();
	void erase_line();
	void erase_display();
	void screen_align();
	void set_margins();
	void respond_id();
	void status_report();
	void set_mode();
	void clear_mode();
	void enable_mode(bool);
	void set_display_attr();
	void set_utf8();
	void clear_utf8();
	void active_g0();
	void active_g1();
	void current_is_g0();
	void current_is_g1();
	void set_charset();
	void set_cursor_type();
	void linux_specific();

	CharAttr normal_char_attr();
	CharAttr erase_char_attr();

	//history
	void history_scroll(u16 num);

	static void init_state();
	static u16 init_history_lines();

	typedef void (VTerm::*StateFunc)();
	struct StateOption {
		u16 key;	// char value to look for, -1 = default/end
		StateFunc action; // 0 = do nothing
		const StateOption *next_state; // 0 = keep current state, -1 = return normal
	};

	static const StateOption control_state[], esc_state[], square_state[], nonstd_state[], percent_state[], hash_state[], charset_state[], funckey_state[];
	static const StateOption *hash_control_state[], *hash_esc_state[], *hash_square_state[];
	const StateOption *current_state;
	bool normal_state;

	//utf8 parse
	u16 utf8_count;
	u32 cur_char;

	//charset
	bool utf8;
	bool g0_is_current;
	u8 charset, g0_charset, g1_charset;
	u8 s_charset, s_g0_charset, s_g1_charset;

	// terminal info
	u16 *text;
	CharAttr *attrs;
	s8 *tab_stops;
	u16 *linenumbers;
	u16 *dirty_startx, *dirty_endx;
	u16 width, height, max_width, max_height;
	u16 scroll_top, scroll_bot;
	s16 pending_scroll; // >0 means scroll up

	// terminal state
	struct ModeFlag {
		ModeFlag();

		u16 display_ctrl : 1;
		u16 toggle_meta : 1;
		u16 crlf : 1;
		u16 auto_wrap : 1;
		u16 insert_mode : 1;
		u16 cursor_visible : 1;
		u16 cursor_relative : 1;
		u16 inverse_screen : 1;
		u16 col_132 : 1;

		u16 numeric_keypad : 1;
		u16 autorepeat_key : 1;
		u16 x10_mouse_report : 1;
		u16 x11_mouse_report : 1;
		u16 cursorkey_esc0 : 1;
	} mode_flags;

	u16 cursor_x, cursor_y, s_cursor_x, s_cursor_y;
	CharAttr char_attr, s_char_attr;

	static const CharAttr default_char_attr;
	s8 default_fcolor, default_bcolor, default_underline_color, default_halfbright_color;

	// action parameters
	u16 npar, param[30];
	bool q_mode;

	//history
	static u16 history_lines;
	bool history_full;
	u32 history_save_line, visual_start_line;
};

#endif
