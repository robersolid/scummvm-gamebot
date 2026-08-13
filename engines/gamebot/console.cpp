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

#include "common/file.h"

#include "gamebot/console.h"
#include "gamebot/gamebot.h"

namespace Gamebot {

Console::Console() : GUI::Debugger() {
	registerCmd("datafiles", WRAP_METHOD(Console, cmdDataFiles));
}

bool Console::cmdDataFiles(int argc, const char **argv) {
	static const char *const fileNames[] = {
		GAMEBOT_RESOURCE_FILE,
		GAMEBOT_ACTIONS_FILE,
		GAMEBOT_NEWGAME_FILE,
		GAMEBOT_NEWGAME_DIALOG_FILE,
	};

	for (const char *name : fileNames) {
		Common::File file;
		if (!file.open(name)) {
			debugPrintf("%-12s <missing>\n", name);
			continue;
		}

		char signature[5] = {};
		file.read(signature, 4);
		uint32 version = file.readUint32LE();
		uint32 count = file.readUint32LE();
		debugPrintf("%-12s %s v%d.%d.%d  %u entries  %u bytes\n",
			name, signature,
			(version >> 24) & 0xff, (version >> 8) & 0xff, version & 0xff,
			count, (uint32)file.size());
	}
	return true;
}

} // End of namespace Gamebot
