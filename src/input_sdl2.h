#ifndef INPUT_SDL2_H
#define INPUT_SDL2_H

#include "input_generic.h"
#include <SDL2/SDL.h>

class Sdl2Input : public KBInput {
	friend class KBInput;
public:
	void processEvent(const SDL_Event &ev);

private:
	static Sdl2Input *initSdl2Input();

	Sdl2Input();
	~Sdl2Input();

	virtual void readyRead(s8 *buf, u32 len) {}

	void handleKeyDown(const SDL_KeyboardEvent &key);
	void handleTextInput(const SDL_TextInputEvent &text);
};

#endif
