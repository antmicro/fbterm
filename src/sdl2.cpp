#include "sdl2.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "io.h"
#include "input_generic.h"
#include "input_sdl2.h"
#include "fbterm.h"
#include "config.h"

#define LOG(fmt, args...) fprintf(stderr, "[sdl2] " fmt "\n", ##args)

#define PRESENT_HZ 60

// Keeps SDL's own event queue serviced (window close/resize/expose/focus)
// even when nothing in fbterm itself is triggering a present().
class Sdl2Presenter : public IoPipe {
public:
	Sdl2Presenter(Sdl2Dev *dev) : mDev(dev)
	{
		s32 timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
		if (timerFd == -1) {
			LOG("timerfd_create: %s", strerror(errno));
			return;
		}

		itimerspec timer = {};
		timer.it_value.tv_nsec = 1000000000 / PRESENT_HZ;
		timer.it_interval.tv_nsec = 1000000000 / PRESENT_HZ;
		timerfd_settime(timerFd, 0, &timer, 0);

		setFd(timerFd);
	}

	~Sdl2Presenter()
	{
		setFd(-1);
	}

private:
	virtual void readyRead(s8 *buf, u32 len)
	{
		mDev->present();
	}

	Sdl2Dev *mDev;
};

bool Sdl2Dev::setup() {
	LOG("probing SDL2 backend");

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		LOG("SDL_Init failed: %s", SDL_GetError());
		return false;
	}

	u32 width = 800, height = 600;
	Config::instance()->getOption("window-width", width);
	Config::instance()->getOption("window-height", height);

	sdlWindow = SDL_CreateWindow("fbterm", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		width, height, 0);
	if (!sdlWindow) {
		LOG("SDL_CreateWindow failed: %s", SDL_GetError());
		SDL_Quit();
		return false;
	}

	sdlWinSurface = SDL_GetWindowSurface(sdlWindow);
	if (!sdlWinSurface) {
		LOG("SDL_GetWindowSurface failed: %s", SDL_GetError());
		SDL_DestroyWindow(sdlWindow);
		SDL_Quit();
		return false;
	}

	sdlDrawSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_XRGB8888);
	if (!sdlDrawSurface) {
		LOG("SDL_CreateRGBSurfaceWithFormat failed: %s", SDL_GetError());
		SDL_DestroyWindow(sdlWindow);
		SDL_Quit();
		return false;
	}

	LOG("window %ux%u created, draw surface pitch=%d", width, height, sdlDrawSurface->pitch);

	mScreenWidth = sdlDrawSurface->w;
	mScreenHeight = sdlDrawSurface->h;
	mWidth = sdlDrawSurface->w;
	mHeight = sdlDrawSurface->h;
	mBytesPerLine = sdlDrawSurface->pitch;
	mVMemBase = (u8 *)sdlDrawSurface->pixels;

	LOG("backend ready: %ux%u-%ubpp, presenting at %dHz", mWidth, mHeight, mBitsPerPixel, PRESENT_HZ);

	presenter = new Sdl2Presenter(this);

	return true;
}

Sdl2Dev *Sdl2Dev::initSdl2Dev()
{
	Sdl2Dev* dev = new Sdl2Dev();

	bool ok = dev->setup();

	if(!ok) {
		delete dev;
		dev = nullptr;
	}


	return dev;
}

Sdl2Dev::Sdl2Dev()
{
	mOffsetLeft = 0;
	mOffsetTop = 0;
	mBitsPerPixel = 32;
	mScrollType = Redraw;
}

Sdl2Dev::~Sdl2Dev()
{
	LOG("shutting down SDL2 backend");

	// IoDispatcher::uninstance() (called before Screen::uninstance() in
	// FbTerm::~FbTerm()) already deleted presenter along with every other
	// still-registered IoPipe - don't delete it again here.

	SDL_FreeSurface(sdlDrawSurface);
	SDL_DestroyWindow(sdlWindow);
	SDL_Quit();
}

void Sdl2Dev::present()
{
	Sdl2Input *input = static_cast<Sdl2Input *>(KBInput::instance());

	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		if (ev.type == SDL_QUIT) {
			LOG("window close requested");
			FbTerm::instance()->exit();
		}
		if (input) input->processEvent(ev);
		// TODO: mouse events
	}

	SDL_BlitSurface(sdlDrawSurface, 0, sdlWinSurface, 0);
	SDL_UpdateWindowSurface(sdlWindow);
}

const s8 *Sdl2Dev::drvId()
{
	return (const s8 *)"sdl2";
}

void Sdl2Dev::setupOffset()
{
}

void Sdl2Dev::setupPalette(bool restore)
{
	if (!restore && !mPalette) return;

	SDL_Color colors[NR_COLORS];
	for (u32 i = 0; i < NR_COLORS; i++) {
		colors[i] = { mPalette[i].red, mPalette[i].green, mPalette[i].blue, 255 };
	}
	SDL_SetPaletteColors(sdlDrawSurface->format->palette, colors, 0, NR_COLORS);
}

