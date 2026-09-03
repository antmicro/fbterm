/*
 *   Copyright © 2008-2010 dragchan <zgchan317@gmail.com>
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

#ifndef MOUSE_H
#define MOUSE_H

#include "io.h"
#include "instance.h"

#ifdef ENABLE_LIBINPUT
struct libinput;
struct libinput_event;
struct libinput_event_pointer;
struct udev;
#endif

class Mouse : public IoPipe {
	DECLARE_INSTANCE(Mouse)
public:
	void switchVc(bool enter);
private:
	virtual void ready(bool isread);
	virtual void readyRead(s8 *buf, u32 len);

	bool mEnabled;

#ifdef ENABLE_LIBINPUT
	void processEvent(struct libinput_event *event);
	void handleMotion(struct libinput_event_pointer *event, bool absolute);
	void handleButton(struct libinput_event_pointer *event);
	void handleScroll(struct libinput_event_pointer *event, s32 eventType);
	void sendEvent(s32 type, s32 buttons);
	void currentCell(u16 &x, u16 &y) const;
	void clampPosition();
	s32 modifierState() const;

	struct libinput *mLibinput;
	struct udev *mUdev;
	double mX;
	double mY;
	double mWheelScroll;
	double mFingerScroll;
	s32 mButtons;
	u64 mLastClickTime;
	u32 mLastClickButton;
	u16 mLastClickX;
	u16 mLastClickY;
	bool mSuspended;
#endif
};

#endif
