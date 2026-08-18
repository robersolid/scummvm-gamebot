/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define FORBIDDEN_SYMBOL_EXCEPTION_getenv
#define FORBIDDEN_SYMBOL_EXCEPTION_putenv
#define FORBIDDEN_SYMBOL_EXCEPTION_setenv
#define FORBIDDEN_SYMBOL_EXCEPTION_readlink

#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>

#include "backends/platform/sdl/webos/webos.h"
#include "backends/events/sdl/sdl-events.h"
#include "common/archive.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/fs.h"

// Magic Remote scancodes of the SDL-webOS fork (stable ABI values;
// not present in stock SDL headers, so they are spelled out here)
enum {
	kScancodeWebosChUp = 480,
	kScancodeWebosChDown = 481,
	kScancodeWebosBack = 482,
	kScancodeWebosCursorShow = 484,
	kScancodeWebosCursorHide = 485,
	kScancodeWebosRed = 486,
	kScancodeWebosGreen = 487,
	kScancodeWebosYellow = 488,
	kScancodeWebosBlue = 489,
	kScancodeWebosExit = 505
};

// SDL_webOSCursorVisibility of SDL-webOS, resolved at runtime so the
// binary also links and runs against a stock SDL
typedef int (*WebOSCursorVisibilityProc)(int);
static WebOSCursorVisibilityProc s_cursorVisibility;

void OSystem_SDL_Webos::hideSystemPointer() {
	if (s_cursorVisibility)
		s_cursorVisibility(0);
}

// Event source mapping the Magic Remote onto ScummVM: BACK is Escape,
// GREEN is Return, YELLOW the main menu, RED the right mouse button,
// BLUE the virtual keyboard and the channel keys page up and down.
// The cursor-show pseudo key the system sends when the remote moves
// re-hides the system pointer at once, so it never comes back over
// ScummVM's own cursor.
class WebosEventSource final : public SdlEventSource {
private:
	uint32 _lastLButtonDownTime = 0;
	uint32 _lastLButtonUpTime = 0;

protected:
	bool dispatchSDLEvent(SDL_Event &ev, Common::Event &event) override {
		if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
			uint32 now = g_system ? g_system->getMillis() : 0;
			if (now - _lastLButtonDownTime < 180)
				return false;
			_lastLButtonDownTime = now;
		} else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
			uint32 now = g_system ? g_system->getMillis() : 0;
			if (now - _lastLButtonUpTime < 180)
				return false;
			_lastLButtonUpTime = now;
		}

		if (ev.type != SDL_KEYDOWN && ev.type != SDL_KEYUP)
			return SdlEventSource::dispatchSDLEvent(ev, event);

		const bool down = (ev.type == SDL_KEYDOWN);
		switch ((int)ev.key.keysym.scancode) {
		case kScancodeWebosCursorShow:
			// The system re-shows its pointer whenever the remote is
			// shaken or moved; put it away again immediately
			if (down)
				OSystem_SDL_Webos::hideSystemPointer();
			return false;
		case kScancodeWebosCursorHide:
			return false;
		case kScancodeWebosBack:
			return mapKey(event, down, Common::KEYCODE_ESCAPE, Common::ASCII_ESCAPE);
		case kScancodeWebosGreen:
			return mapKey(event, down, Common::KEYCODE_RETURN, Common::ASCII_RETURN);
		case kScancodeWebosYellow:
			return mapKey(event, down, Common::KEYCODE_F5, Common::ASCII_F5);
		case kScancodeWebosChUp:
			return mapKey(event, down, Common::KEYCODE_PAGEUP, 0);
		case kScancodeWebosChDown:
			return mapKey(event, down, Common::KEYCODE_PAGEDOWN, 0);
		case kScancodeWebosRed:
			event.type = down ? Common::EVENT_RBUTTONDOWN : Common::EVENT_RBUTTONUP;
			return processMouseEvent(event, _mouseX, _mouseY);
		case kScancodeWebosBlue:
			if (!down)
				return false;
			event.type = Common::EVENT_VIRTUAL_KEYBOARD;
			return true;
		case kScancodeWebosExit:
			if (!down)
				return false;
			event.type = Common::EVENT_QUIT;
			return true;
		default:
			return SdlEventSource::dispatchSDLEvent(ev, event);
		}
	}

private:
	static bool mapKey(Common::Event &event, bool down, Common::KeyCode key, uint16 ascii) {
		event.type = down ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
		event.kbd = Common::KeyState(key, ascii);
		return true;
	}
};

void OSystem_SDL_Webos::init() {
	// webOS specific hints of SDL-webOS; they must be set before the
	// SDL video init and are ignored by a stock SDL. The app handles
	// the back and exit keys itself (mapped above), and the system
	// pointer auto-hides quickly as a fallback for the explicit hide.
	SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_BACK", "true");
	SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_EXIT", "true");
	SDL_SetHint("SDL_WEBOS_CURSOR_SLEEP_TIME", "1");
	SDL_SetHint("SDL_WEBOS_REGISTER_APP", "true");
	SDL_SetHint("SDL_TOUCH_MOUSE_EVENTS", "1");
	SDL_SetHint("SDL_MOUSE_TOUCH_EVENTS", "1");
	// GPU linear filtering is free on the TV scaler
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

	_eventSource = new WebosEventSource();

	OSystem_POSIX::init();
}

void OSystem_SDL_Webos::initBackend() {
	char exePath[512] = {0};
	ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
	Common::String dataDir = "data";
	if (len > 0) {
		exePath[len] = '\0';
		char *lastSlash = strrchr(exePath, '/');
		if (lastSlash) {
			*lastSlash = '\0';
			dataDir = Common::String::format("%s/data", exePath);
		}
	}

	// TV friendly defaults: full screen at the panel resolution with
	// GPU scaling and filtering, and the TV's native audio rate
	ConfMan.registerDefault("fullscreen", true);
	ConfMan.registerDefault("filtering", true);
	ConfMan.registerDefault("output_rate", 48000);
	ConfMan.registerDefault("gfx_mode", "opengl");
	ConfMan.registerDefault("themepath", dataDir);
	ConfMan.registerDefault("extrapath", dataDir);
	ConfMan.registerDefault("gui_theme", "scummmodern");

	SearchMan.addDirectory("webos_data", Common::FSNode(Common::Path(dataDir)), 0, 2);

	s_cursorVisibility =
		(WebOSCursorVisibilityProc)dlsym(RTLD_DEFAULT, "SDL_webOSCursorVisibility");

	OSystem_POSIX::initBackend();

	hideSystemPointer();
}
