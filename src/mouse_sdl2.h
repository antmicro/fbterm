#ifndef MOUSE_SDL2_H
#define MOUSE_SDL2_H

#include "mouse_generic.h"
#include <SDL2/SDL.h>

class Sdl2Mouse : public GenericMouse {
	friend class GenericMouse;
public:
	void processEvent(const SDL_Event &ev);

private:
	static Sdl2Mouse *initSdl2Mouse();

	Sdl2Mouse();
	~Sdl2Mouse();

	virtual void readyRead(s8 *buf, u32 len) {}

	void handleMotion(const SDL_MouseMotionEvent &motion);
	void handleButton(const SDL_MouseButtonEvent &button);
	void handleWheel(const SDL_MouseWheelEvent &wheel);
	void sendEvent(s32 type, s32 buttons);
	void currentCell(u16 &x, u16 &y) const;
	void clampPosition();
	s32 modifierState() const;

	bool mEnabled;
	double mX;
	double mY;
	s32 mButtons;
	u64 mLastClickTime;
	u32 mLastClickButton;
	u16 mLastClickX;
	u16 mLastClickY;
};

#endif
