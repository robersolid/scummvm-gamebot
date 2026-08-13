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
#include "common/endian.h"
#include "common/events.h"
#include "common/file.h"
#include "common/tokenizer.h"
#include "engines/util.h"
#include "graphics/cursorman.h"
#include "graphics/framelimiter.h"

#include "gamebot/gamebot.h"
#include "gamebot/console.h"
#include "gamebot/debug-names.h"

namespace Gamebot {

GamebotEngine *g_engine;

GamebotEngine::GamebotEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc), _randomSource("gamebot") {
	g_engine = this;
}

GamebotEngine::~GamebotEngine() {
	delete[] _standardCursor.pixels;
	delete[] _hotCursor.pixels;
	delete _screen;
}

// Cursor images belong to the MouseSys object (id 3); layout is a
// rect, the hotspot point and the 8bpp pixels
void GamebotEngine::loadCursor(uint32 resId, Cursor &cursor) {
	static const uint32 kMouseSysObjectId = 3;
	int i = _resources.findObject(kMouseSysObjectId);
	for (; i >= 0 && i < (int)_resources.count() &&
			_resources.entry(i).objectId == kMouseSysObjectId; i++) {
		const ResourceEntry &e = _resources.entry(i);
		if (e.resId != resId || e.type != kResMouseImage)
			continue;
		byte *data = _resources.readBlob(e);
		if (!data)
			return;
		cursor.width = (uint16)(READ_LE_INT32(data + 8) - READ_LE_INT32(data) + 1);
		cursor.height = (uint16)(READ_LE_INT32(data + 12) - READ_LE_INT32(data + 4) + 1);
		cursor.hotX = (uint16)READ_LE_UINT32(data + 16);
		cursor.hotY = (uint16)READ_LE_UINT32(data + 20);
		cursor.pixels = new byte[cursor.width * cursor.height];
		memcpy(cursor.pixels, data + 24, cursor.width * cursor.height);
		delete[] data;
		return;
	}
	warning("Mouse cursor resource %08x not found", resId);
}

void GamebotEngine::setCursor(const Cursor &cursor) {
	if (!cursor.pixels)
		return;
	CursorMan.replaceCursor(cursor.pixels, cursor.width, cursor.height,
		cursor.hotX, cursor.hotY, 0);
}

void GamebotEngine::handleMouseMove(const Common::Point &screenPos) {
	Common::Point phasePos(screenPos.x + _world.origin().x, screenPos.y + _world.origin().y);
	HitResult hit;
	bool hovering = _world.hitTest(phasePos, hit);

	if (hovering && hit.objectId != _hoverObjectId)
		debugC(kDebugEvents, "Hovering %08x '%s'", hit.objectId, hit.name.c_str());
	_hoverObjectId = hovering ? hit.objectId : 0;

	if (hovering != _hotCursorShown) {
		setCursor(hovering ? _hotCursor : _standardCursor);
		_hotCursorShown = hovering;
	}
}

void GamebotEngine::handleMouseClick(const Common::Point &screenPos) {
	Common::Point phasePos(screenPos.x + _world.origin().x, screenPos.y + _world.origin().y);
	HitResult hit;
	if (!_world.hitTest(phasePos, hit)) {
		debugC(kDebugEvents, "Click on nothing at (%d,%d)", phasePos.x, phasePos.y);
		return;
	}

	debugC(kDebugEvents, "Click on %08x '%s' (%s)", hit.objectId, hit.name.c_str(),
		resourceTypeName(hit.type));

	// Phase exits already work: walking there comes with the characters
	if (hit.exitPhase && _world.gotoPhase(hit.exitPhase))
		debugC(kDebugEvents, "Exit taken to phase %08x", hit.exitPhase);
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

	// Show something real until the script system decides the phase:
	// the chapter 1 map screen
	if (!_world.currentPhaseId())
		_world.gotoPhase(0x101);

	// Standard and hot cursor of the original mouse handler
	loadCursor(0x00030001, _standardCursor);
	loadCursor(0x00030002, _hotCursor);
	setCursor(_standardCursor);
	CursorMan.showMouse(true);

	Common::Event e;
	Graphics::FrameLimiter limiter(g_system, 60);
	while (!shouldQuit()) {
		while (g_system->getEventManager()->pollEvent(e)) {
			switch (e.type) {
			case Common::EVENT_MOUSEMOVE:
				handleMouseMove(e.mouse);
				break;
			case Common::EVENT_LBUTTONDOWN:
				handleMouseClick(e.mouse);
				break;
			case Common::EVENT_KEYDOWN:
				// Debug scrolling of wide phases until the camera
				// follows the characters
				if (e.kbd.keycode == Common::KEYCODE_RIGHT)
					_world.origin().x = MIN<int16>(_world.origin().x + 16,
						MAX(0, _world.phaseWidth() - kScreenWidth));
				else if (e.kbd.keycode == Common::KEYCODE_LEFT)
					_world.origin().x = MAX<int16>(_world.origin().x - 16, 0);
				break;
			default:
				break;
			}
		}

		_world.update(g_system->getMillis());
		_world.draw(_screen);
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
