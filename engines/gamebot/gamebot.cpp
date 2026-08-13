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
#include "common/substream.h"
#include "common/tokenizer.h"
#include "engines/util.h"
#include "graphics/cursorman.h"
#include "graphics/framelimiter.h"
#include "graphics/paletteman.h"
#include "video/flic_decoder.h"

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

	// While an object hangs from the cursor its image stays put
	if (!_linkedObject && hovering != _hotCursorShown) {
		setCursor(hovering ? _hotCursor : _standardCursor);
		_hotCursorShown = hovering;
	}
}

// Swaps the mouse cursor for the linked object's inventory image
// (the original MouseSys object-linking), or restores the cursor
void GamebotEngine::linkObject(uint32 objectId) {
	_linkedObject = objectId;
	if (!objectId) {
		setCursor(_standardCursor);
		_hotCursorShown = false;
		return;
	}
	int16 width = 0, height = 0;
	const byte *pixels = _inventoryUI.itemCursor(objectId, width, height);
	if (pixels)
		CursorMan.replaceCursor(pixels, width, height, width / 2, height / 2, 0);
	debugC(kDebugEvents, "Item %08x linked to the cursor", objectId);
}

void GamebotEngine::handleMouseClick(const Common::Point &screenPos) {
	// An open conversation captures every click
	if (_logic.handleDialogClick(screenPos))
		return;

	// The inventory: picking an item hangs it from the cursor; the
	// inventory stays open and right click closes it
	if (_inventoryUI.isOpen()) {
		uint32 item = _inventoryUI.handleClick(screenPos);
		if (item)
			linkObject(item);
		return;
	}

	// The medallion switches control to the partner agent
	if (_changerBadge.handleClick(screenPos))
		return;

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
		if (_linkedObject)
			linkObject(0);
		master().walkTo(_world, phasePos);
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
		linkObject(0);
		_logic.interactWith(hit.objectId, kVerbUse, item);
		return;
	}

	_verbPalette.open(screenPos, hit.objectId);
}

bool GamebotEngine::playVideo(uint32 flicResId) {
	const ResourceEntry *e = _resources.findByResId(flicResId);
	if (!e || e->type != kResAnimationFlic) {
		warning("Video %08x not found", flicResId);
		return false;
	}

	// The blob only holds the sound code and the location of the FLC
	// stream elsewhere in the resource file
	byte *params = _resources.readBlob(*e);
	if (!params)
		return false;
	uint32 soundCode = READ_LE_UINT32(params);
	uint32 flicLocation = READ_LE_UINT32(params + 4);
	uint32 flicSize = READ_LE_UINT32(params + 8);
	delete[] params;

	// The decoder gets its own file handle: the mixer may still be
	// streaming music from the shared one when we start
	Common::File *file = new Common::File();
	if (!file->open(GAMEBOT_RESOURCE_FILE)) {
		delete file;
		return false;
	}
	Common::SeekableSubReadStream *stream = new Common::SeekableSubReadStream(
		file, flicLocation, flicLocation + flicSize, DisposeAfterUse::YES);

	Video::FlicDecoder decoder;
	if (!decoder.loadStream(stream)) {
		warning("Video %08x is not a valid FLC", flicResId);
		return false;
	}

	debugC(kDebugResources, "Playing video %08x: %d frames %dx%d, sound %08x",
		flicResId, decoder.getFrameCount(), decoder.getWidth(), decoder.getHeight(), soundCode);
	_sounds.stopAll();
	if (soundCode)
		_sounds.playSound(soundCode);
	decoder.start();

	bool skipped = false;
	while (!shouldQuit() && !decoder.endOfVideo() && !skipped) {
		if (decoder.needsUpdate()) {
			const Graphics::Surface *frame = decoder.decodeNextFrame();
			if (frame) {
				if (decoder.hasDirtyPalette())
					g_system->getPaletteManager()->setPalette(decoder.getPalette(), 0, 256);
				_screen->blitFrom(*frame);
				_screen->update();
			}
		}

		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_LBUTTONDOWN ||
					(event.type == Common::EVENT_KEYDOWN &&
					event.kbd.keycode == Common::KEYCODE_ESCAPE))
				skipped = true;
		}
		g_system->delayMillis(10);
	}

	_sounds.stopSound();
	_world.applyPalette();
	if (_world.musicCode())
		_sounds.playMusic(_world.musicCode());
	return true;
}

// Toggles control between Mortadelo and Filemon when both are in
// the phase (the original ChangerMaster swap)
void GamebotEngine::switchMaster() {
	if (_master == &_mortadelo && _filemon.visible && _filemon.isLoaded())
		_master = &_filemon;
	else if (_master == &_filemon && _mortadelo.visible && _mortadelo.isLoaded())
		_master = &_mortadelo;
	else
		return;
	debugC(kDebugEvents, "Master is now %08x", _master->objectId());
}

bool GamebotEngine::gotoPhase(uint32 phaseId) {
	if (!_world.gotoPhase(phaseId))
		return false;

	// Rule-driven object changes persist across phase loads
	_logic.applyObjectStates();

	// Place the characters at their phase entry locations: a phase
	// either hosts Mortadelo and Filemon or the combined character
	int phaseIndex = _initialWorld.findPhase(phaseId);
	if (phaseIndex >= 0) {
		const PhaseEntry &phase = _initialWorld.phase(phaseIndex);
		_mortadelo.visible = _filemon.visible = _both.visible = false;
		for (uint i = 0; i < 2; i++) {
			const CharacterLocation &location = phase.chars[i];
			Character *character =
				(location.characterId == _mortadelo.objectId()) ? &_mortadelo :
				(location.characterId == _filemon.objectId()) ? &_filemon :
				(location.characterId == _both.objectId()) ? &_both : nullptr;
			if (!character || !character->isLoaded() || !(location.x || location.y))
				continue;
			character->visible = true;
			character->enterPhase(_world, location);
		}
		// The combined character rules the map screens; otherwise
		// keep the current master if present, defaulting to Mortadelo
		if (_both.visible)
			_master = &_both;
		else if (_mortadelo.visible && (_master == &_both || !_master->visible))
			_master = &_mortadelo;
		else if (!_master->visible && _filemon.visible)
			_master = &_filemon;
	}

	// Phases can carry full-screen videos (the logo and intro chain);
	// they play on entry and their end events drive the phase chain.
	// A chained phase change aborts the scan of the old phase.
	if (phaseIndex >= 0) {
		const PhaseEntry &phase = _initialWorld.phase(phaseIndex);
		for (uint32 l = 1; l < phase.layerCount && _world.currentPhaseId() == phaseId; l++) {
			const LayerEntry &layer = _initialWorld.layer(phase.layerFirst + l);
			for (uint32 o = 0; o < layer.objectCount && _world.currentPhaseId() == phaseId; o++) {
				const ObjectEntry &object = _initialWorld.object(layer.objectFirst + o);
				if (object.objectId < 0x100)
					continue;
				const ResourceEntry *flic = _resources.findResource(object.objectId, kResAnimationFlic);
				if (flic && playVideo(flic->resId))
					_logic.onAnimationEnded(flic->resId);
			}
		}
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
	_filemon.load(kCharFilemon);
	_both.load(kCharBoth);
	_changerBadge.load();

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

	// Boot like the original: the starting phase is wherever the
	// characters live in default.def, which chains the logo and
	// intro videos into the game
	if (!_world.currentPhaseId()) {
		uint32 bootPhase = _initialWorld.phaseContaining(_mortadelo.objectId());
		gotoPhase(bootPhase ? bootPhase : 0x101);
	}

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
				else if (e.kbd.keycode == Common::KEYCODE_TAB)
					switchMaster();
				break;
			default:
				break;
			}
		}

		uint32 millis = g_system->getMillis();
		_world.update(millis);
		_mortadelo.tick(millis, _world);
		_filemon.tick(millis, _world);
		_both.tick(millis, _world);
		_changerBadge.update(millis);
		_logic.update(millis);

		// Camera follows the master character on wide phases,
		// at most 5 pixels per frame (original Character::Redraw)
		if (master().isLoaded() && _world.phaseWidth() > kScreenWidth) {
			int maxOrigin = _world.phaseWidth() - kScreenWidth;
			int screenCenter = kScreenWidth / 2 + _world.origin().x;
			int diff = master().x() - screenCenter;
			if (diff > 0)
				_world.origin().x = MIN<int16>(_world.origin().x + MIN(5, diff), maxOrigin);
			else if (diff < 0)
				_world.origin().x = MAX<int16>(_world.origin().x - MIN(5, -diff), 0);
		}

		_world.draw(_screen, &master(), secondCharacter());
		_logic.writer().draw(_screen);
		_logic.drawDialog(_screen);
		_changerBadge.draw(_screen);
		_verbPalette.draw(_screen);
		_inventoryUI.draw(_screen);
		limiter.delayBeforeSwap();
		_screen->update();
		limiter.startFrame();
	}

	return Common::kNoError;
}

Common::Error GamebotEngine::syncGame(Common::Serializer &s) {
	if (!s.syncVersion(1))
		return Common::kUnknownError;

	uint32 phaseId = _world.currentPhaseId();
	uint32 masterId = master().objectId();
	int16 x = master().x(), y = master().y();
	uint16 orient = master().orientation(), layer = master().layer();
	s.syncAsUint32LE(phaseId);
	s.syncAsUint32LE(masterId);
	s.syncAsSint16LE(x);
	s.syncAsSint16LE(y);
	s.syncAsUint16LE(orient);
	s.syncAsUint16LE(layer);

	_logic.syncGame(s);

	if (s.isLoading()) {
		if (!_world.gotoPhase(phaseId))
			return Common::kUnknownError;
		_logic.applyObjectStates();
		_master = (masterId == _filemon.objectId()) ? &_filemon :
			(masterId == _both.objectId()) ? &_both : &_mortadelo;
		CharacterLocation location;
		location.characterId = masterId;
		location.x = x;
		location.y = y;
		location.orientation = orient;
		location.layer = layer;
		master().enterPhase(_world, location);
		master().visible = true;
		debugC(kDebugSaves, "Game loaded: phase %08x, character at (%d,%d)", phaseId, x, y);
	} else {
		debugC(kDebugSaves, "Game saved: phase %08x, character at (%d,%d)", phaseId, x, y);
	}
	return Common::kNoError;
}

} // End of namespace Gamebot
