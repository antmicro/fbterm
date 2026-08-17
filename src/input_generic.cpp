#include "input_generic.h"
#include "config.h"
#include "input.h"
#ifdef ENABLE_SDL2
#include "input_sdl2.h"
#endif

DEFINE_INSTANCE(KBInput)

KBInput::KBInput()
{
}

KBInput::~KBInput()
{
}

KBInput *KBInput::createInstance()
{
	KBInput* input = nullptr;

#ifdef ENABLE_SDL2
	bool sdl = false;
	Config::instance()->getOption("use-sdl", sdl);
	if(sdl)
		input = Sdl2Input::initSdl2Input();
#endif
	if(!input)
		input = TtyInput::initTtyInput();

	return input;
}
