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

#include "vterm.h"

#define KEEP (VTerm::StateOption*)0
#define NORMAL (VTerm::StateOption*)-1
#define ADDSAME(len) ((len) << 8)

const VTerm::StateOption VTerm::control_state[] = {
    { 0,	0,	KEEP },
	{ 7,	&VTerm::bell,	KEEP },
	{ 8,	&VTerm::bs,		KEEP },
	{ 9,	&VTerm::tab,	KEEP },
	{ 0xA,	&VTerm::lf,	KEEP },
	{ 0xB,	&VTerm::lf,	KEEP },
	{ 0xC,	&VTerm::lf,	KEEP },
	{ 0xD,	&VTerm::cr,	KEEP },
	{ 0xE,	&VTerm::active_g1,	KEEP },
	{ 0xF,	&VTerm::active_g0,	KEEP },
	{ 0x18, 0,	NORMAL },
	{ 0x1A, 0,	NORMAL },
	{ 0x1B, 0,	esc_state },
	{ 0x7F, 0,	KEEP },
	{ 0x9B, 0,	square_state },
	{ -1 }
};

const VTerm::StateOption VTerm::esc_state[] = {
	{ '[', &VTerm::clear_param,	square_state },
	{ ']', &VTerm::clear_param,	nonstd_state },
	{ '%', 0,	percent_state },
	{ '#', 0,	hash_state },
	{ '(', &VTerm::current_is_g0,	charset_state },
	{ ')', &VTerm::current_is_g1,	charset_state },
	{ 'c', &VTerm::reset,		NORMAL },
	{ 'D', &VTerm::index_down,	NORMAL },
	{ 'E', &VTerm::next_line,	NORMAL },
	{ 'H', &VTerm::set_tab,		NORMAL },
	{ 'M', &VTerm::index_up,	NORMAL },
	{ 'Z', &VTerm::respond_id,	NORMAL },
	{ '7', &VTerm::save_cursor,	NORMAL },
	{ '8', &VTerm::restore_cursor,	NORMAL },
	{ '>', &VTerm::keypad_numeric,	NORMAL },
	{ '=', &VTerm::keypad_application,	NORMAL },
	{ -1 }
};

const VTerm::StateOption VTerm::square_state[] = {
	{ '[', 0,	funckey_state },
	{ '?', &VTerm::set_q_mode,	KEEP },
	{ '0' | ADDSAME(9), &VTerm::param_digit,	KEEP },
	{ ';', &VTerm::next_param,	KEEP },
	{ '@', &VTerm::insert_char,	NORMAL },
	{ 'A', &VTerm::cursor_up,	NORMAL },
	{ 'B', &VTerm::cursor_down,	NORMAL },
	{ 'C', &VTerm::cursor_right,NORMAL },
	{ 'D', &VTerm::cursor_left,	NORMAL },
	{ 'E', &VTerm::cursor_down_cr, NORMAL },
	{ 'F', &VTerm::cursor_up_cr, NORMAL },
	{ 'G', &VTerm::cursor_position_col,	NORMAL },
	{ 'H', &VTerm::cursor_position,	NORMAL },
	{ 'J', &VTerm::erase_display,	NORMAL },
	{ 'K', &VTerm::erase_line,	NORMAL },
	{ 'L', &VTerm::insert_line, NORMAL },
	{ 'M', &VTerm::delete_line,	NORMAL },
	{ 'P', &VTerm::delete_char,	NORMAL },
	{ 'X', &VTerm::erase_char,	NORMAL },
	{ 'a', &VTerm::cursor_right,NORMAL },
	{ 'c', &VTerm::set_cursor_type,	NORMAL },
	{ 'd', &VTerm::cursor_position_row, NORMAL },
	{ 'e', &VTerm::cursor_down,	NORMAL },
	{ 'f', &VTerm::cursor_position,	NORMAL },
	{ 'g', &VTerm::clear_tab,	NORMAL },
	{ 'h', &VTerm::set_mode,	NORMAL },
	{ 'l', &VTerm::clear_mode,	NORMAL },
	{ 'm', &VTerm::set_display_attr,	NORMAL },
	{ 'n', &VTerm::status_report,	NORMAL },
	{ 'q', &VTerm::set_led, NORMAL },
	{ 'r', &VTerm::set_margins,	NORMAL },
	{ 's', &VTerm::save_cursor,	NORMAL },
	{ 'u', &VTerm::restore_cursor,	NORMAL },
	{ '`', &VTerm::cursor_position_col,	NORMAL },
	{ ']', &VTerm::linux_specific, NORMAL },
	{ -1 }
};

const VTerm::StateOption VTerm::nonstd_state[] = {
	{ '0' | ADDSAME(9), &VTerm::param_digit,	KEEP },
	{ 'A' | ADDSAME(5), &VTerm::param_digit,	KEEP },
	{ 'a' | ADDSAME(5), &VTerm::param_digit,	KEEP },
	{ 'P', &VTerm::set_palette, KEEP },
	{ 'R', &VTerm::reset_palette, NORMAL },
	{ -1 }
};

const VTerm::StateOption VTerm::percent_state[] = {
	{ '@', &VTerm::clear_utf8,	NORMAL },
	{ 'G', &VTerm::set_utf8,	NORMAL },
	{ '8', &VTerm::set_utf8,	NORMAL },
	{ -1 }
};

const VTerm::StateOption VTerm::charset_state[] = {
	{ '0', &VTerm::set_charset, NORMAL },
	{ 'B', &VTerm::set_charset, NORMAL },
	{ 'U', &VTerm::set_charset, NORMAL },
	{ 'K', &VTerm::set_charset, NORMAL },
	{ -1 }
};

const VTerm::StateOption VTerm::hash_state[] = {
	{ '8', &VTerm::screen_align,	NORMAL },
	{ -1 }
};

const VTerm::StateOption VTerm::funckey_state[] = {
	{ -1 }
};
