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
	if (_inventoryUI.isOpen()) {
		_inventoryUI.updateHover(screenPos);
		return;
	}
	if (_verbPalette.isOpen()) {
		_verbPalette.updateHover(screenPos);
		return;
	}

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
	// An open conversation captures every click
	if (_logic.handleDialogClick(screenPos))
		return;

	// The inventory: picking an item selects it for a use-with
	if (_inventoryUI.isOpen()) {
		uint32 item = _inventoryUI.handleClick(screenPos);
		if (item) {
			_linkedObject = item;
			_inventoryUI.close();
			debugC(kDebugEvents, "Item %08x selected for use", item);
		}
		return;
	}

	// An open verb palette resolves the click into a verb
	if (_verbPalette.isOpen()) {
		Verb verb;
		uint32 target = _verbPalette.targetObject();
		if (_verbPalette.handleClick(screenPos, verb))
			_logic.interactWith(target, verb);
		return;
	}

	Common::Point phasePos(screenPos.x + _world.origin().x, screenPos.y + _world.origin().y);
	HitResult hit;
	if (!_world.hitTest(phasePos, hit)) {
		// Clicking the floor walks there
		debugC(kDebugEvents, "Click on floor at (%d,%d)", phasePos.x, phasePos.y);
		_linkedObject = 0;
		_mortadelo.walkTo(_world, phasePos);
		return;
	}

	debugC(kDebugEvents, "Click on %08x '%s' (%s)", hit.objectId, hit.name.c_str(),
		resourceTypeName(hit.type));

	if (hit.exitPhase) {
		if (gotoPhase(hit.exitPhase))
			debugC(kDebugEvents, "Exit taken to phase %08x", hit.exitPhase);
		return;
	}

	if (_linkedObject) {
		// Use the selected inventory object on the clicked one
		uint32 item = _linkedObject;
		_linkedObject = 0;
		_logic.interactWith(hit.objectId, kVerbUse, item);
		return;
	}

	_verbPalette.open(screenPos, hit.objectId);
}

bool GamebotEngine::gotoPhase(uint32 phaseId) {
	if (!_world.gotoPhase(phaseId))
		return false;

	// Place the characters at the phase entry location
	int phaseIndex = _initialWorld.findPhase(phaseId);
	if (phaseIndex >= 0 && _mortadelo.isLoaded()) {
		const PhaseEntry &phase = _initialWorld.phase(phaseIndex);
		const CharacterLocation *location = &phase.chars[0];
		for (uint i = 0; i < 2; i++) {
			if (phase.chars[i].characterId == _mortadelo.objectId())
				location = &phase.chars[i];
		}
		_mortadelo.visible = location->x || location->y;
		if (_mortadelo.visible)
			_mortadelo.enterPhase(_world, *location);
	}
	return true;
}

uint32 GamebotEngine::getFeatures() const {
	return _gameDescription->flags;
}

Common::String GamebotEngine::getGameId() const {
	return _gameDescription->gameId;
}

bool GamebotEngine::loadDataFiles() {
	if (!_resources.load(GAMEBOT_RESOURCE_FILE) ||
			!_actions.load(GAMEBOT_ACTIONS_FILE) ||
			!_initialWorld.load(GAMEBOT_NEWGAME_FILE))
		return false;

	// Initial dialog states; optional, sentences fall back to the
	// pristine copies inside the resource file
	_dialogFile.load(GAMEBOT_NEWGAME_DIALOG_FILE);
	return true;
}

Common::Error GamebotEngine::run() {
	initGraphics(kScreenWidth, kScreenHeight);
	_screen = new Graphics::Screen();

	Console *console = new Console();
	setDebugger(console);

	if (!loadDataFiles())
		return Common::kNoGameDataFoundError;

	_mortadelo.load(kCharMortadelo);

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
		gotoPhase(0x101);

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
			case Common::EVENT_RBUTTONDOWN:
				// Right click toggles the inventory safe
				if (!_logic.isDialogOpen()) {
					_verbPalette.close();
					_inventoryUI.toggle();
				}
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

		uint32 millis = g_system->getMillis();
		_world.update(millis);
		_mortadelo.tick(millis, _world);
		_logic.update(millis);

		// Camera follows the master character on wide phases,
		// at most 5 pixels per frame (original Character::Redraw)
		if (_mortadelo.isLoaded() && _world.phaseWidth() > kScreenWidth) {
			int maxOrigin = _world.phaseWidth() - kScreenWidth;
			int screenCenter = kScreenWidth / 2 + _world.origin().x;
			int diff = _mortadelo.x() - screenCenter;
			if (diff > 0)
				_world.origin().x = MIN<int16>(_world.origin().x + MIN(5, diff), maxOrigin);
			else if (diff < 0)
				_world.origin().x = MAX<int16>(_world.origin().x - MIN(5, -diff), 0);
		}

		_world.draw(_screen, &_mortadelo);
		_logic.writer().draw(_screen);
		_logic.drawDialog(_screen);
		_verbPalette.draw(_screen);
		_inventoryUI.draw(_screen);
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
