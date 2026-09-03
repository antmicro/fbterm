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

#include "mouse.h"
#include "config.h"
#include "fbshellman.h"
#include "fbshell.h"
#include "fbterm.h"
#include "screen.h"

DEFINE_INSTANCE(Mouse)

#ifdef ENABLE_LIBINPUT

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/keyboard.h>
#include <linux/tiocl.h>
#include <libinput.h>
#include <libudev.h>

static int open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags | O_CLOEXEC);
	return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *user_data)
{
	close(fd);
}

static const struct libinput_interface libinput_interface = {
	open_restricted,
	close_restricted,
};

#endif

Mouse *Mouse::createInstance()
{
	return new Mouse();
}

Mouse::Mouse()
	: mEnabled(false)
#ifdef ENABLE_LIBINPUT
	, mLibinput(0)
	, mUdev(0)
	, mX(0)
	, mY(0)
	, mWheelScroll(0)
	, mFingerScroll(0)
	, mButtons(0)
	, mLastClickTime(0)
	, mLastClickButton(0)
	, mLastClickX(0)
	, mLastClickY(0)
	, mSuspended(false)
#endif
{
#ifdef ENABLE_LIBINPUT
	bool useMouse = false;
	Config::instance()->getOption("use-mouse", useMouse);
	if (!useMouse) return;

	mUdev = udev_new();
	if (!mUdev) {
		fprintf(stderr, "can't initialize udev for mouse input\n");
		return;
	}

	mLibinput = libinput_udev_create_context(&libinput_interface, this, mUdev);
	if (!mLibinput) {
		fprintf(stderr, "can't initialize libinput\n");
		udev_unref(mUdev);
		mUdev = 0;
		return;
	}

	if (libinput_udev_assign_seat(mLibinput, "seat0")) {
		fprintf(stderr, "can't assign libinput seat0\n");
		libinput_unref(mLibinput);
		udev_unref(mUdev);
		mLibinput = 0;
		mUdev = 0;
		return;
	}

	// IoPipe owns and closes its descriptor. Keep libinput's descriptor owned
	// by libinput and monitor a duplicate of it in FbTerm's dispatcher.
	s32 fd = dup(libinput_get_fd(mLibinput));
	if (fd == -1) {
		fprintf(stderr, "can't monitor libinput file descriptor\n");
		libinput_unref(mLibinput);
		udev_unref(mUdev);
		mLibinput = 0;
		mUdev = 0;
		return;
	}

	setFd(fd);
	mEnabled = true;
#endif
}

Mouse::~Mouse()
{
#ifdef ENABLE_LIBINPUT
	if (mLibinput) libinput_unref(mLibinput);
	if (mUdev) udev_unref(mUdev);
#endif
}

void Mouse::readyRead(s8 *buf, u32 len)
{
	// libinput owns reads from its fd; Mouse::ready() dispatches it directly.
}

void Mouse::ready(bool isread)
{
#ifdef ENABLE_LIBINPUT
	if (!isread || !mEnabled || !mLibinput || mSuspended) return;
	if (libinput_dispatch(mLibinput)) return;

	struct libinput_event *event;
	while ((event = libinput_get_event(mLibinput))) {
		processEvent(event);
		libinput_event_destroy(event);
	}
#else
	(void)isread;
#endif
}

void Mouse::switchVc(bool enter)
{
#ifdef ENABLE_LIBINPUT
	if (!mEnabled || !mLibinput) return;

	if (!enter && !mSuspended) {
		libinput_suspend(mLibinput);
		mSuspended = true;
	} else if (enter && mSuspended) {
		if (!libinput_resume(mLibinput)) {
			mSuspended = false;
			libinput_dispatch(mLibinput);
		} else {
			fprintf(stderr, "can't resume libinput\n");
		}
	}
#else
	(void)enter;
#endif
}

#ifdef ENABLE_LIBINPUT

void Mouse::processEvent(struct libinput_event *event)
{
	s32 type = libinput_event_get_type(event);
	struct libinput_event_pointer *pointer;

	switch (type) {
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		mButtons = 0;
		break;

	case LIBINPUT_EVENT_POINTER_MOTION:
		pointer = libinput_event_get_pointer_event(event);
		FbTerm::instance()->notifyActivity();
		handleMotion(pointer, false);
		break;

	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		pointer = libinput_event_get_pointer_event(event);
		FbTerm::instance()->notifyActivity();
		handleMotion(pointer, true);
		break;

	case LIBINPUT_EVENT_POINTER_BUTTON:
		pointer = libinput_event_get_pointer_event(event);
		FbTerm::instance()->notifyActivity();
		handleButton(pointer);
		break;

	case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
	case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
	case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS:
		pointer = libinput_event_get_pointer_event(event);
		FbTerm::instance()->notifyActivity();
		handleScroll(pointer, type);
		break;

	default:
		break;
	}
}

void Mouse::clampPosition()
{
	Screen *screen = Screen::instance();
	double maxX = screen->width() ? screen->width() - 1 : 0;
	double maxY = screen->height() ? screen->height() - 1 : 0;

	if (mX < 0) mX = 0;
	else if (mX > maxX) mX = maxX;

	if (mY < 0) mY = 0;
	else if (mY > maxY) mY = maxY;
}

void Mouse::currentCell(u16 &x, u16 &y) const
{
	Screen *screen = Screen::instance();
	u32 width = screen->width();
	u32 height = screen->height();
	u16 cols = screen->cols();
	u16 rows = screen->rows();

	if (!width || !height || !cols || !rows) {
		x = y = 0;
		return;
	}

	x = (u16)(mX * cols / width);
	y = (u16)(mY * rows / height);
	if (x >= cols) x = cols - 1;
	if (y >= rows) y = rows - 1;
}

void Mouse::handleMotion(struct libinput_event_pointer *event, bool absolute)
{
	u16 oldX, oldY, newX, newY;
	currentCell(oldX, oldY);

	Screen *screen = Screen::instance();
	if (absolute) {
		if (!screen->width() || !screen->height()) return;
		mX = libinput_event_pointer_get_absolute_x_transformed(event, screen->width());
		mY = libinput_event_pointer_get_absolute_y_transformed(event, screen->height());
	} else {
		mX += libinput_event_pointer_get_dx(event);
		mY += libinput_event_pointer_get_dy(event);
	}

	clampPosition();
	currentCell(newX, newY);
	if (newX == oldX && newY == oldY) return;

	sendEvent(Move, mButtons);
}

void Mouse::handleButton(struct libinput_event_pointer *event)
{
	u32 code = libinput_event_pointer_get_button(event);
	s32 button;

	switch (code) {
	case BTN_LEFT:
		button = LeftButton;
		break;
	case BTN_MIDDLE:
		button = MidButton;
		break;
	case BTN_RIGHT:
		button = RightButton;
		break;
	default:
		return;
	}

	if (libinput_event_pointer_get_button_state(event) == LIBINPUT_BUTTON_STATE_PRESSED) {
		mButtons |= button;

		u16 x, y;
		currentCell(x, y);
		u64 time = libinput_event_pointer_get_time_usec(event);
		s32 type = Press;

		if (mLastClickTime && time >= mLastClickTime
				&& time - mLastClickTime <= 500000
				&& code == mLastClickButton
				&& x == mLastClickX && y == mLastClickY) {
			type = DblClick;
			mLastClickTime = 0;
		} else {
			mLastClickTime = time;
			mLastClickButton = code;
			mLastClickX = x;
			mLastClickY = y;
		}

		sendEvent(type, button);
	} else {
		sendEvent(Release, button);
		mButtons &= ~button;
	}
}

void Mouse::handleScroll(struct libinput_event_pointer *event, s32 eventType)
{
	if (!libinput_event_pointer_has_axis(event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) return;

	double *accumulator;
	double step;

	if (eventType == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL) {
		mWheelScroll += libinput_event_pointer_get_scroll_value_v120(
			event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		accumulator = &mWheelScroll;
		step = 120.0;
	} else {
		double value = libinput_event_pointer_get_scroll_value(
			event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
		if (!value) {
			mFingerScroll = 0;
			return;
		}
		mFingerScroll += value;
		accumulator = &mFingerScroll;

		Screen *screen = Screen::instance();
		step = screen->rows() ? (double)screen->height() / screen->rows() : 1.0;
		if (step < 1.0) step = 1.0;
	}

	while (*accumulator >= step) {
		sendEvent(Wheel, WheelDown);
		*accumulator -= step;
	}
	while (*accumulator <= -step) {
		sendEvent(Wheel, WheelUp);
		*accumulator += step;
	}
}

s32 Mouse::modifierState() const
{
	u8 state = TIOCL_GETSHIFTSTATE;
	if (ioctl(STDIN_FILENO, TIOCLINUX, &state) == -1) return 0;

	s32 modifiers = 0;
	if (state & (1 << KG_SHIFT)) modifiers |= ShiftButton;
	if (state & (1 << KG_CTRL)) modifiers |= ControlButton;
	if (state & ((1 << KG_ALT) | (1 << KG_ALTGR))) modifiers |= AltButton;
	return modifiers;
}

void Mouse::sendEvent(s32 type, s32 buttons)
{
	FbShell *shell = FbShellManager::instance()->activeShell();
	if (!shell) return;

	u16 x, y;
	currentCell(x, y);
	shell->mouseInput(x, y, type, buttons | modifierState());
}

#endif
