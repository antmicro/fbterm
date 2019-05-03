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

#ifndef INPUT_KEY_H
#define INPUT_KEY_H

#include <linux/keyboard.h>

#define AC(code) (K(KT_LATIN, (code)))
#define START_CODE 0x80

#define SHIFT_PAGEUP 	AC(START_CODE)
#define SHIFT_PAGEDOWN 	AC(START_CODE + 1)
#define CTRL_ALT_1 	AC(START_CODE + 2)
#define CTRL_ALT_2 	AC(START_CODE + 3)
#define CTRL_ALT_3 	AC(START_CODE + 4)
#define CTRL_ALT_4 	AC(START_CODE + 5)
#define CTRL_ALT_5 	AC(START_CODE + 6)
#define CTRL_ALT_6 	AC(START_CODE + 7)
#define CTRL_ALT_7 	AC(START_CODE + 8)
#define CTRL_ALT_8 	AC(START_CODE + 9)
#define CTRL_ALT_9 	AC(START_CODE + 10)
#define CTRL_ALT_0 	AC(START_CODE + 11)
#define CTRL_ALT_C 	AC(START_CODE + 12)
#define CTRL_ALT_D 	AC(START_CODE + 13)
#define CTRL_ALT_E 	AC(START_CODE + 14)
#define SHIFT_LEFT	AC(START_CODE + 15)
#define SHIFT_RIGHT	AC(START_CODE + 16)
#define CTRL_ALT_F1 AC(START_CODE + 17)
#define CTRL_ALT_F2 AC(START_CODE + 18)
#define CTRL_ALT_F3 AC(START_CODE + 19)
#define CTRL_ALT_F4 AC(START_CODE + 20)
#define CTRL_ALT_F5 AC(START_CODE + 21)
#define CTRL_ALT_F6 AC(START_CODE + 22)

#define AC_START SHIFT_PAGEUP
#define AC_END CTRL_ALT_F6

#endif
