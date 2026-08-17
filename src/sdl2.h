#ifndef SDL2_H
#define SDL2_H

#include "screen.h"
#include <SDL2/SDL.h>

class Sdl2Dev : public Screen {
private:
	friend class Screen;
	static Sdl2Dev *initSdl2Dev();

	Sdl2Dev();
	~Sdl2Dev();

	virtual void setupOffset();
	virtual void setupPalette(bool restore);
	virtual const s8 *drvId();

	friend class Sdl2Presenter;
	void present();

	SDL_Window *sdlWindow = nullptr;
	SDL_Surface *sdlWinSurface = nullptr;
	SDL_Surface *sdlDrawSurface = nullptr;
	class Sdl2Presenter *presenter = nullptr;
	bool setup();
};
#endif // SDL2_H
