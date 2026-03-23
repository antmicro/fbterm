/*
 *   Copyright © 2008-2010 dragchan <zgchan317@gmail.com>
 *   This file is part of FbTerm.
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

// Allows the same state mapping to be repeated 'len' times for the following ASCII characters
#define ADDSAME(len) ((len) << 8)
#define ENDSEQ  { ((u16)-1) }
#define CSPAN(start, end) start | ADDSAME(end - start)

const VTerm::Sequence VTerm::control_sequences[] = {
	{ 0x00, nullptr, ESkeep },
	{ 0x07, &VTerm::bell, ESkeep },
	{ 0x08, &VTerm::bs, ESkeep },
	{ 0x09, &VTerm::tab, ESkeep },
	{ 0x0A, &VTerm::lf, ESkeep },
	{ 0x0B, &VTerm::lf, ESkeep },
	{ 0x0C, &VTerm::lf, ESkeep },
	{ 0x0D, &VTerm::cr, ESkeep },
	{ 0x0E, &VTerm::active_g1, ESkeep },
	{ 0x0F, &VTerm::active_g0, ESkeep },
	{ 0x18, nullptr, ESnormal },
	{ 0x1A, nullptr, ESnormal },
	{ 0x1B, nullptr, ESesc },
	{ 0x7F, nullptr, ESkeep },
	{ 0x9B, nullptr, EScsi },
	ENDSEQ
};

const VTerm::Sequence VTerm::escape_sequences[] = {

	// ESnormal #0
	{ 0, nullptr, ESnormal },
	{ 0, nullptr, ESnormal }, // default fallthough case
	ENDSEQ,

	// ESesc #1 "ESC"
	{ '[', &VTerm::clear_param,	EScsi },
	{ ']', &VTerm::clear_param,	ESosc },
	{ 'P', &VTerm::clear_param, ESdcs },
	{ '%', nullptr, ESpercent },
	{ '#', nullptr, EShash },
	{ '(', &VTerm::current_is_g0,	EScharset },
	{ ')', &VTerm::current_is_g1,	EScharset },
	{ 'Z', &VTerm::get_device_attribute, ESnormal }, // Obsolete form of 'CSI c'
	{ 'c', &VTerm::reset,		ESnormal },
	{ 'D', &VTerm::index_down,	ESnormal },
	{ 'E', &VTerm::next_line,	ESnormal },
	{ 'H', &VTerm::set_tab,		ESnormal },
	{ 'M', &VTerm::index_up,	ESnormal },
	{ 'Z', &VTerm::respond_id,	ESnormal },
	{ '7', &VTerm::save_cursor,	ESnormal },
	{ '8', &VTerm::restore_cursor,	ESnormal },
	{ '>', &VTerm::keypad_numeric,	ESnormal },
	{ '=', &VTerm::keypad_application,	ESnormal },
	ENDSEQ,

	// EScsi #2 "ESC ["
	{ '?', &VTerm::set_q_mode,	ESkeep },
	{ '>', nullptr, ESgreater },
	{ CSPAN('0', '9'), &VTerm::param_digit, ESkeep },
	{ ';', &VTerm::next_param, ESkeep },
	{ ':', &VTerm::next_param, ESkeep }, // some codes use ':' instead of ';' as the argument separator
	{ '@', &VTerm::insert_char,	ESnormal },
	{ 'A', &VTerm::cursor_up,	ESnormal },
	{ 'B', &VTerm::cursor_down,	ESnormal },
	{ 'C', &VTerm::cursor_right,ESnormal },
	{ 'D', &VTerm::cursor_left,	ESnormal },
	{ 'E', &VTerm::cursor_down_cr, ESnormal },
	{ 'F', &VTerm::cursor_up_cr, ESnormal },
	{ 'G', &VTerm::cursor_position_col,	ESnormal },
	{ 'H', &VTerm::cursor_position,	ESnormal },
	{ 'J', &VTerm::erase_display,	ESnormal },
	{ 'K', &VTerm::erase_line,	ESnormal },
	{ 'L', &VTerm::insert_line, ESnormal },
	{ 'M', &VTerm::delete_line,	ESnormal },
	{ 'P', &VTerm::delete_char,	ESnormal },
	{ 'X', &VTerm::erase_char,	ESnormal },
	{ 'a', &VTerm::cursor_right,ESnormal },
	{ 'c', &VTerm::set_cursor_type,	ESnormal },
	{ 'd', &VTerm::cursor_position_row, ESnormal },
	{ 'e', &VTerm::cursor_down,	ESnormal },
	{ 'f', &VTerm::cursor_position,	ESnormal },
	{ 'g', &VTerm::clear_tab,	ESnormal },
	{ 'h', &VTerm::set_mode,	ESnormal },
	{ 'l', &VTerm::clear_mode,	ESnormal },
	{ 'm', &VTerm::set_display_attr,	ESnormal },
	{ 'n', &VTerm::status_report,	ESnormal },
	{ 'q', &VTerm::set_led, ESnormal },
	{ 'r', &VTerm::set_margins,	ESnormal },
	{ 's', &VTerm::save_cursor,	ESnormal },
	{ 't', &VTerm::window_ops,      ESnormal },
	{ 'u', &VTerm::restore_cursor,	ESnormal },
	{ '`', &VTerm::cursor_position_col,	ESnormal },
	{ ']', &VTerm::linux_specific, ESnormal },
	{ '}', &VTerm::fbterm_specific, ESnormal },
	{ '%', 0, ESkeep }, // this is a workaround for the wierd, undocumented code "\e[0%m" - otherwise the trailing 'm' gets printed
	ENDSEQ,

	// ESosc #3 "ESC ]"
	{ CSPAN(' ', '~'), nullptr, ESkeep }, // consume printable characters
	{ CSPAN('0', '9'), &VTerm::param_any_digit, ESkeep },
	{ CSPAN('a', 'f'), &VTerm::param_any_digit, ESkeep },
	{ CSPAN('A', 'F'), &VTerm::param_any_digit, ESkeep },
	{ '#', &VTerm::begin_hex, ESkeep },
	{ ';', &VTerm::next_param, ESkeep },
	{ 0x07, &VTerm::osc_end, ESnormal },
	{ 0x1b, &VTerm::osc_end, ESst },

// FBTerm specific palette codes
//	{ '0' | ADDSAME(9), &VTerm::set_palette,    ESkeep },
//	{ 'A' | ADDSAME(5), &VTerm::set_palette,    ESkeep },
//	{ 'a' | ADDSAME(5), &VTerm::set_palette,    ESkeep },
//	{ 'P', &VTerm::begin_set_palette, ESkeep },
//	{ 'R', &VTerm::reset_palette, ESnormal },
	ENDSEQ,

	// ESpercent #4 "ESC %"
	{ '@', &VTerm::clear_utf8,	ESnormal },
	{ 'G', &VTerm::set_utf8,	ESnormal },
	{ '8', &VTerm::set_utf8,	ESnormal },
	ENDSEQ,

	// EScharset #5 "ESC )" or "ESC ("
	{ '0', &VTerm::set_charset, ESnormal },
	{ 'B', &VTerm::set_charset, ESnormal },
	{ 'U', &VTerm::set_charset, ESnormal },
	{ 'K', &VTerm::set_charset, ESnormal },
	ENDSEQ,

	// EShash #6 "ESC #"
	{ '8', &VTerm::screen_align, ESnormal },
	{ '9', &VTerm::screen_clear, ESnormal },
	ENDSEQ,

	// ESgreater #8 "ESC [ >"
	{ CSPAN('0', '9'), &VTerm::param_digit, ESkeep },
	{ ';', &VTerm::next_param,	ESkeep },
	{ 'c', &VTerm::get_device_attribute, ESnormal }, // Send Device Attributes (Secondary DA)
	{ 'm', &VTerm::set_key_modifier, ESnormal }, // Set/reset key modifier options
	ENDSEQ,

	// ESdcs #9 "ESC P"
	{ '+', nullptr, ESkeep },
	{ 'q', nullptr, EStermcap },
	ENDSEQ,

	// EStermcap #10 "ESC P + q"
	{ CSPAN('0', '9'), &VTerm::param_hex_digit, ESkeep },
	{ CSPAN('a', 'f'), &VTerm::param_hex_digit, ESkeep },
	{ CSPAN('A', 'F'), &VTerm::param_hex_digit, ESkeep },
	{ 0x1B, &VTerm::request_termcap, ESst },
	ENDSEQ,

	// ESst #11
	{ '\\', nullptr, ESnormal },
	ENDSEQ

};
