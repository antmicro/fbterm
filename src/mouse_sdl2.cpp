#include "mouse_sdl2.h"
#include "config.h"
#include "fbshellman.h"
#include "fbshell.h"
#include "fbterm.h"
#include "screen.h"

Sdl2Mouse *Sdl2Mouse::initSdl2Mouse()
{
	return new Sdl2Mouse();
}

Sdl2Mouse::Sdl2Mouse()
	: mEnabled(false)
	, mX(0)
	, mY(0)
	, mButtons(0)
	, mLastClickTime(0)
	, mLastClickButton(0)
	, mLastClickX(0)
	, mLastClickY(0)
{
	Config::instance()->getOption("use-mouse", mEnabled);
}

Sdl2Mouse::~Sdl2Mouse()
{
}

void Sdl2Mouse::clampPosition()
{
	Screen *screen = Screen::instance();
	double maxX = screen->width() ? screen->width() - 1 : 0;
	double maxY = screen->height() ? screen->height() - 1 : 0;

	if (mX < 0) mX = 0;
	else if (mX > maxX) mX = maxX;

	if (mY < 0) mY = 0;
	else if (mY > maxY) mY = maxY;
}

void Sdl2Mouse::currentCell(u16 &x, u16 &y) const
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

void Sdl2Mouse::handleMotion(const SDL_MouseMotionEvent &motion)
{
	u16 oldX, oldY, newX, newY;
	currentCell(oldX, oldY);

	mX = motion.x;
	mY = motion.y;

	clampPosition();
	currentCell(newX, newY);
	if (newX == oldX && newY == oldY) return;

	sendEvent(Move, mButtons);
}

void Sdl2Mouse::handleButton(const SDL_MouseButtonEvent &event)
{
	s32 button;

	switch (event.button) {
	case SDL_BUTTON_LEFT:
		button = LeftButton;
		break;
	case SDL_BUTTON_MIDDLE:
		button = MidButton;
		break;
	case SDL_BUTTON_RIGHT:
		button = RightButton;
		break;
	default:
		return;
	}

	if (event.type == SDL_MOUSEBUTTONDOWN) {
		mButtons |= button;

		u16 x, y;
		currentCell(x, y);
		u64 time = SDL_GetTicks64();
		s32 type = Press;

		if (mLastClickTime && time >= mLastClickTime
				&& time - mLastClickTime <= 500
				&& event.button == mLastClickButton
				&& x == mLastClickX && y == mLastClickY) {
			type = DblClick;
			mLastClickTime = 0;
		} else {
			mLastClickTime = time;
			mLastClickButton = event.button;
			mLastClickX = x;
			mLastClickY = y;
		}

		sendEvent(type, button);
	} else {
		sendEvent(Release, button);
		mButtons &= ~button;
	}
}

void Sdl2Mouse::handleWheel(const SDL_MouseWheelEvent &wheel)
{
	s32 amount = wheel.y;
	if (wheel.direction == SDL_MOUSEWHEEL_FLIPPED) amount = -amount;

	while (amount > 0) {
		sendEvent(Wheel, WheelUp);
		amount--;
	}
	while (amount < 0) {
		sendEvent(Wheel, WheelDown);
		amount++;
	}
}

s32 Sdl2Mouse::modifierState() const
{
	SDL_Keymod mod = SDL_GetModState();
	s32 modifiers = 0;

	if (mod & KMOD_SHIFT) modifiers |= ShiftButton;
	if (mod & KMOD_CTRL) modifiers |= ControlButton;
	if (mod & KMOD_ALT) modifiers |= AltButton;

	return modifiers;
}

void Sdl2Mouse::sendEvent(s32 type, s32 buttons)
{
	FbShell *shell = FbShellManager::instance()->activeShell();
	if (!shell) return;

	u16 x, y;
	currentCell(x, y);
	shell->mouseInput(x, y, type, buttons | modifierState());
}

void Sdl2Mouse::processEvent(const SDL_Event &ev)
{
	if (!mEnabled) return;

	switch (ev.type) {
	case SDL_MOUSEMOTION:
		FbTerm::instance()->notifyActivity();
		handleMotion(ev.motion);
		break;

	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		FbTerm::instance()->notifyActivity();
		handleButton(ev.button);
		break;

	case SDL_MOUSEWHEEL:
		FbTerm::instance()->notifyActivity();
		handleWheel(ev.wheel);
		break;

	default:
		break;
	}
}
