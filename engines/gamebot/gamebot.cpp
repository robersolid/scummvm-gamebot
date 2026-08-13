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

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/file.h"
#include "engines/util.h"
#include "graphics/framelimiter.h"

#include "gamebot/gamebot.h"
#include "gamebot/console.h"

namespace Gamebot {

GamebotEngine *g_engine;

GamebotEngine::GamebotEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc), _randomSource("gamebot") {
	g_engine = this;
}

GamebotEngine::~GamebotEngine() {
	delete _screen;
}

uint32 GamebotEngine::getFeatures() const {
	return _gameDescription->flags;
}

Common::String GamebotEngine::getGameId() const {
	return _gameDescription->gameId;
}

bool GamebotEngine::verifyDataFiles() {
	// All GameBot data files share the same 8-byte header layout
	// (see BotFile.h in the original sources): a 4-char signature
	// followed by a version dword, then per-format counters.
	struct FileCheck {
		const char *name;
		const char *signature;
		bool mandatory;
	};
	static const FileCheck checks[] = {
		{ GAMEBOT_RESOURCE_FILE, "WZRS", true },
		{ GAMEBOT_ACTIONS_FILE, "WZAC", true },
		{ GAMEBOT_NEWGAME_FILE, "WZOB", true },
		{ GAMEBOT_NEWGAME_DIALOG_FILE, "WZRS", false },
	};

	for (const FileCheck &check : checks) {
		Common::File file;
		if (!file.open(check.name)) {
			if (!check.mandatory)
				continue;
			warning("Could not open data file %s", check.name);
			return false;
		}

		char signature[4];
		if (file.read(signature, 4) != 4 || memcmp(signature, check.signature, 4) != 0) {
			warning("Data file %s has an invalid signature", check.name);
			return false;
		}

		uint32 version = file.readUint32LE();
		uint32 count = file.readUint32LE();
		debugC(kDebugResources, "%s: version 0x%08x, %u entries", check.name, version, count);
	}
	return true;
}

Common::Error GamebotEngine::run() {
	initGraphics(kScreenWidth, kScreenHeight);
	_screen = new Graphics::Screen();

	setDebugger(new Console());

	if (!verifyDataFiles())
		return Common::kNoGameDataFoundError;

	// If a savegame was selected from the launcher, load it
	int saveSlot = ConfMan.getInt("save_slot");
	if (saveSlot != -1)
		(void)loadGameState(saveSlot);

	// Minimal event loop: a black screen that can be quit.
	// The actual game loop arrives with the world and video subsystems.
	Common::Event e;
	Graphics::FrameLimiter limiter(g_system, 60);
	while (!shouldQuit()) {
		while (g_system->getEventManager()->pollEvent(e)) {
		}

		limiter.delayBeforeSwap();
		_screen->update();
		limiter.startFrame();
	}

	return Common::kNoError;
}

Common::Error GamebotEngine::syncGame(Common::Serializer &s) {
	// Savegame layout is defined in a later milestone; version the
	// stream from the very first save to stay forward compatible.
	if (!s.syncVersion(1))
		return Common::kUnknownError;

	return Common::kNoError;
}

} // End of namespace Gamebot
