#include "mouse_generic.h"
#include "config.h"
#include "mouse.h"
#ifdef ENABLE_SDL2
#include "mouse_sdl2.h"
#endif

DEFINE_INSTANCE(GenericMouse)

GenericMouse::GenericMouse()
{
}

GenericMouse::~GenericMouse()
{
}

GenericMouse *GenericMouse::createInstance()
{
	GenericMouse *mouse = 0;

#ifdef ENABLE_SDL2
	bool sdl = false;
	Config::instance()->getOption("use-sdl", sdl);
	if (sdl)
		mouse = Sdl2Mouse::initSdl2Mouse();
#endif
	if (!mouse)
		mouse = LibinputMouse::initLibinputMouse();

	return mouse;
}
