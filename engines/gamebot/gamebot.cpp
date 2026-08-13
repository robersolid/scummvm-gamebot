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
#include "common/tokenizer.h"
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

bool GamebotEngine::loadDataFiles() {
	return _resources.load(GAMEBOT_RESOURCE_FILE) &&
		_actions.load(GAMEBOT_ACTIONS_FILE) &&
		_initialWorld.load(GAMEBOT_NEWGAME_FILE);
}

Common::Error GamebotEngine::run() {
	initGraphics(kScreenWidth, kScreenHeight);
	_screen = new Graphics::Screen();

	Console *console = new Console();
	setDebugger(console);

	if (!loadDataFiles())
		return Common::kNoGameDataFoundError;

	// Development aid: run semicolon-separated console commands from
	// the config file, e.g. gamebot_exec=phases;dumpmap 0x0101
	if (ConfMan.hasKey("gamebot_exec")) {
		Common::StringTokenizer commands(ConfMan.get("gamebot_exec"), ";");
		while (!commands.empty())
			console->executeCommand(commands.nextToken());
	}

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
