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
#define FORBIDDEN_SYMBOL_EXCEPTION_setenv
#define FORBIDDEN_SYMBOL_EXCEPTION_putenv

#include <stdlib.h>
#include <unistd.h>

#include "backends/platform/sdl/webos/webos.h"
#include "backends/plugins/sdl/sdl-provider.h"
#include "base/main.h"

int main(int argc, char *argv[]) {
	if (!getenv("XDG_RUNTIME_DIR"))
		setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 0);
	if (!getenv("WAYLAND_DISPLAY"))
		setenv("WAYLAND_DISPLAY", "wayland-0", 0);
	if (!getenv("APPID") && !getenv("WEBOS_APP_ID"))
		setenv("APPID", "org.scummvm.webos", 0);
	if (!getenv("SDL_WEBOS_CURSOR_SLEEP_TIME"))
		setenv("SDL_WEBOS_CURSOR_SLEEP_TIME", "1", 0);

	g_system = new OSystem_SDL_Webos();
	assert(g_system);

	g_system->init();

#ifdef DYNAMIC_MODULES
	PluginManager::instance().addPluginProvider(new SDLPluginProvider());
#endif

	// Filter out webOS SAM launch arguments (which are passed as JSON strings starting with '{')
	int newArgc = 0;
	char *newArgv[128];
	for (int i = 0; i < argc && newArgc < 127; ++i) {
		if (argv[i] && argv[i][0] == '{')
			continue;
		newArgv[newArgc++] = argv[i];
	}
	newArgv[newArgc] = nullptr;

	int res = scummvm_main(newArgc, newArgv);

	g_system->destroy();

	return res;
}
