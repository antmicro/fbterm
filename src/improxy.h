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

#ifndef IM_PROXY_H
#define IM_PROXY_H

#include "type.h"
#include "instance.h"

class ImProxy {
	DECLARE_INSTANCE(ImProxy)
public:
	bool actived();
	void toggleActive(u32 shell);
	void sendKey(s8 *buf, u32 len);
	void changeCursorPos(u16 col, u16 row);
	void changeTermMode(bool crlf, bool appkey, bool curo);

private:
	friend class ImSocket;
	void createImProcess();
	void socketEnd();

	s32 mPid;
	class ImSocket *mSocket;
};

#endif
