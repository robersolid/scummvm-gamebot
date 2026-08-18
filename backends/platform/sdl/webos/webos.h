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

#ifndef PLATFORM_SDL_WEBOS_H
#define PLATFORM_SDL_WEBOS_H

#include "backends/platform/sdl/posix/posix.h"

// Port for LG webOS TVs (homebrew), built against the webosbrew
// SDL-webOS fork with the webosbrew native toolchain. The webOS
// specific bits (system pointer, Magic Remote keys) resolve at
// runtime, so the same binary runs on a stock SDL for testing.
class OSystem_SDL_Webos : public OSystem_POSIX {
public:
	void init() override;
	void initBackend() override;

	// Hides the system Magic-Remote pointer so only ScummVM's own
	// cursor shows; safe no-op outside SDL-webOS
	static void hideSystemPointer();
};

#endif
