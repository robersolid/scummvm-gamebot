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

#ifndef GAMEBOT_CONSOLE_H
#define GAMEBOT_CONSOLE_H

#include "gui/debugger.h"

namespace Gamebot {

struct ResourceEntry;

class Console : public GUI::Debugger {
private:
	// Palette used when dumping images as BMP, selectable per phase
	byte _dumpPalette[256 * 3];

	bool cmdDataFiles(int argc, const char **argv);
	bool cmdResInfo(int argc, const char **argv);
	bool cmdPhases(int argc, const char **argv);
	bool cmdObjects(int argc, const char **argv);
	bool cmdActions(int argc, const char **argv);
	bool cmdDumpRes(int argc, const char **argv);
	bool cmdDumpMap(int argc, const char **argv);
	bool cmdPalette(int argc, const char **argv);
	bool cmdGoto(int argc, const char **argv);
	bool cmdScroll(int argc, const char **argv);
	bool cmdScreenshot(int argc, const char **argv);
	bool cmdOverlay(int argc, const char **argv);

	uint dumpImageBlob(const ResourceEntry &e, const byte *data);
	bool dumpSurface(const byte *pixels, uint16 width, uint16 height,
		const Common::String &fileName);

public:
	Console();
	~Console() override {}

	// Runs a console command programmatically; used by the gamebot_exec
	// config key to script debug commands from the command line
	bool executeCommand(const Common::String &command);
};

} // End of namespace Gamebot

#endif // GAMEBOT_CONSOLE_H
