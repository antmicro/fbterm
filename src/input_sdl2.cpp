#include "input_sdl2.h"
#include "input_key.h"
#include "fbshell.h"
#include "fbshellman.h"
#include "fbterm.h"

#include <string.h>

Sdl2Input *Sdl2Input::initSdl2Input()
{
	SDL_StartTextInput();
	return new Sdl2Input();
}

Sdl2Input::Sdl2Input()
{
}

Sdl2Input::~Sdl2Input()
{
}

struct SysKeyEntry {
	SDL_Keycode sym;
	u16 mods;
	u16 key;
};

// Mirrors TtyInput's sysKeyTable, but matched against SDL's own
// keysym/modifier model instead of the Linux VT keymap remap hack.
static const SysKeyEntry sysKeyTable[] = {
	{ SDLK_PAGEUP,   KMOD_SHIFT, SHIFT_PAGEUP },
	{ SDLK_PAGEDOWN, KMOD_SHIFT, SHIFT_PAGEDOWN },
	{ SDLK_LEFT,     KMOD_SHIFT, SHIFT_LEFT },
	{ SDLK_RIGHT,    KMOD_SHIFT, SHIFT_RIGHT },
	{ SDLK_SPACE,    KMOD_CTRL,  CTRL_SPACE },
	{ SDLK_1, KMOD_CTRL | KMOD_ALT, CTRL_ALT_1 },
	{ SDLK_2, KMOD_CTRL | KMOD_ALT, CTRL_ALT_2 },
	{ SDLK_3, KMOD_CTRL | KMOD_ALT, CTRL_ALT_3 },
	{ SDLK_4, KMOD_CTRL | KMOD_ALT, CTRL_ALT_4 },
	{ SDLK_5, KMOD_CTRL | KMOD_ALT, CTRL_ALT_5 },
	{ SDLK_6, KMOD_CTRL | KMOD_ALT, CTRL_ALT_6 },
	{ SDLK_7, KMOD_CTRL | KMOD_ALT, CTRL_ALT_7 },
	{ SDLK_8, KMOD_CTRL | KMOD_ALT, CTRL_ALT_8 },
	{ SDLK_9, KMOD_CTRL | KMOD_ALT, CTRL_ALT_9 },
	{ SDLK_0, KMOD_CTRL | KMOD_ALT, CTRL_ALT_0 },
	{ SDLK_c, KMOD_CTRL | KMOD_ALT, CTRL_ALT_C },
	{ SDLK_d, KMOD_CTRL | KMOD_ALT, CTRL_ALT_D },
	{ SDLK_e, KMOD_CTRL | KMOD_ALT, CTRL_ALT_E },
	{ SDLK_F1, KMOD_CTRL | KMOD_ALT, CTRL_ALT_F1 },
	{ SDLK_F2, KMOD_CTRL | KMOD_ALT, CTRL_ALT_F2 },
	{ SDLK_F3, KMOD_CTRL | KMOD_ALT, CTRL_ALT_F3 },
	{ SDLK_F4, KMOD_CTRL | KMOD_ALT, CTRL_ALT_F4 },
	{ SDLK_F5, KMOD_CTRL | KMOD_ALT, CTRL_ALT_F5 },
	{ SDLK_F6, KMOD_CTRL | KMOD_ALT, CTRL_ALT_F6 },
	{ SDLK_k, KMOD_CTRL | KMOD_ALT, CTRL_ALT_K },
};

// Keys that don't arrive via SDL_TEXTINPUT (arrows, navigation, function
// keys, control keys) and need translating into the escape sequences a
// terminal application expects. Arrow keys respect "application cursor
// key mode" (DECCKM / CursorKeyEscO) same as a real terminal would.
static bool keyToEscapeSeq(const SDL_KeyboardEvent &key, FbShell *shell, s8 *buf, u32 &len)
{
	bool appCursor = shell->mode(VTerm::CursorKeyEscO);
	const s8 *seq = 0;

	switch (key.keysym.sym) {
	case SDLK_UP:       seq = appCursor ? "\eOA" : "\e[A"; break;
	case SDLK_DOWN:     seq = appCursor ? "\eOB" : "\e[B"; break;
	case SDLK_RIGHT:    seq = appCursor ? "\eOC" : "\e[C"; break;
	case SDLK_LEFT:     seq = appCursor ? "\eOD" : "\e[D"; break;
	case SDLK_HOME:     seq = "\e[1~"; break;
	case SDLK_END:      seq = "\e[4~"; break;
	case SDLK_INSERT:   seq = "\e[2~"; break;
	case SDLK_DELETE:   seq = "\e[3~"; break;
	case SDLK_PAGEUP:   seq = "\e[5~"; break;
	case SDLK_PAGEDOWN: seq = "\e[6~"; break;
	case SDLK_F1:  seq = "\e[[A"; break;
	case SDLK_F2:  seq = "\e[[B"; break;
	case SDLK_F3:  seq = "\e[[C"; break;
	case SDLK_F4:  seq = "\e[[D"; break;
	case SDLK_F5:  seq = "\e[[E"; break;
	case SDLK_F6:  seq = "\e[17~"; break;
	case SDLK_F7:  seq = "\e[18~"; break;
	case SDLK_F8:  seq = "\e[19~"; break;
	case SDLK_F9:  seq = "\e[20~"; break;
	case SDLK_F10: seq = "\e[21~"; break;
	case SDLK_F11: seq = "\e[23~"; break;
	case SDLK_F12: seq = "\e[24~"; break;
	case SDLK_BACKSPACE: seq = "\x7f"; break;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:  seq = "\r"; break;
	case SDLK_TAB:       seq = "\t"; break;
	case SDLK_ESCAPE:    seq = "\e"; break;
	default: return false;
	}

	len = strlen(seq);
	memcpy(buf, seq, len);
	return true;
}

void Sdl2Input::handleKeyDown(const SDL_KeyboardEvent &key)
{
	u16 mods = 0;
	if (key.keysym.mod & KMOD_CTRL) mods |= KMOD_CTRL;
	if (key.keysym.mod & KMOD_ALT) mods |= KMOD_ALT;
	if (key.keysym.mod & KMOD_SHIFT) mods |= KMOD_SHIFT;

	for (u32 i = 0; i < sizeof(sysKeyTable) / sizeof(sysKeyTable[0]); i++) {
		if (sysKeyTable[i].sym == key.keysym.sym && sysKeyTable[i].mods == mods) {
			FbTerm::instance()->processSysKey(sysKeyTable[i].key);
			return;
		}
	}

	FbShell *shell = FbShellManager::instance()->activeShell();
	if (!shell) return;

	// Ctrl+letter -> control code (Ctrl+A=0x01 .. Ctrl+Z=0x1A); Ctrl+Alt
	// combos are already claimed by sysKeyTable above.
	if ((mods & KMOD_CTRL) && !(mods & KMOD_ALT) && key.keysym.sym >= SDLK_a && key.keysym.sym <= SDLK_z) {
		s8 c = (s8)((key.keysym.sym - SDLK_a) + 1);
		shell->keyInput(&c, 1);
		FbTerm::instance()->notifyActivity();
		return;
	}

	s8 buf[16];
	u32 len = 0;
	if (keyToEscapeSeq(key, shell, buf, len)) {
		shell->keyInput(buf, len);
		FbTerm::instance()->notifyActivity();
	}
}

void Sdl2Input::handleTextInput(const SDL_TextInputEvent &text)
{
	FbShell *shell = FbShellManager::instance()->activeShell();
	if (!shell) return;

	u32 len = strlen(text.text);
	if (!len) return;

	shell->keyInput((s8 *)text.text, len);
	FbTerm::instance()->notifyActivity();
}

void Sdl2Input::processEvent(const SDL_Event &ev)
{
	switch (ev.type) {
	case SDL_KEYDOWN:
		handleKeyDown(ev.key);
		break;

	case SDL_TEXTINPUT:
		handleTextInput(ev.text);
		break;

	default:
		break;
	}
}
