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
#include "graphics/font.h"
#include "graphics/fontman.h"
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
	_lastMousePhasePos = Common::Point(
		screenPos.x + _world.origin().x, screenPos.y + _world.origin().y);
	if (_mainMenu.isOpen()) {
		if (_optionsPanels.isOpen())
			_optionsPanels.updateHover(screenPos);
		else
			_mainMenu.updateHover(screenPos);
		return;
	}
	if (_inventoryUI.isOpen()) {
		_inventoryUI.updateHover(screenPos);
		return;
	}
	if (_verbPalette.isOpen()) {
		// Leaving the palette rectangle cancels the selection, as the
		// original select mode does
		if (!_verbPalette.contains(screenPos))
			_verbPalette.close();
		else
			_verbPalette.updateHover(screenPos);
		return;
	}

	Common::Point phasePos(screenPos.x + _world.origin().x, screenPos.y + _world.origin().y);
	HitResult hit;
	bool hovering = _world.hitTest(phasePos, hit);

	if (hovering && hit.objectId != _hoverObjectId)
		debugC(kDebugEvents, "Hovering %08x '%s'", hit.objectId, hit.name.c_str());
	_hoverObjectId = hovering ? hit.objectId : 0;
	_hoverName = hovering ? hit.name : Common::String();

	if (hovering != _hotCursorShown) {
		if (_linkedObject)
			// The carried object swaps to its red-outlined image while
			// it hovers an interactable (original evObjSetInvImage)
			applyLinkedCursor(hovering);
		else
			setCursor(hovering ? _hotCursor : _standardCursor);
		_hotCursorShown = hovering;
	}
}

// Puts the linked object's inventory image on the cursor, in its
// normal or highlighted (red outline) variant
void GamebotEngine::applyLinkedCursor(bool highlighted) {
	int16 width = 0, height = 0;
	const byte *pixels = _inventoryUI.itemCursor(_linkedObject, width, height, highlighted);
	if (pixels)
		CursorMan.replaceCursor(pixels, width, height, width / 2, height / 2, 0);
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
	applyLinkedCursor(false);
	_hotCursorShown = false;
	debugC(kDebugEvents, "Item %08x linked to the cursor", objectId);
}

// Runs a main menu button; the load, save and options screens use
// the ScummVM dialogs instead of the original panels
void GamebotEngine::runMenuAction(int action) {
	switch (action) {
	case MainMenu::kActionNewGame:
		_mainMenu.close();
		_logic.resetGame();
		break;
	case MainMenu::kActionLoad:
		_optionsPanels.open(OptionsPanels::kPanelLoad);
		break;
	case MainMenu::kActionSave:
		_optionsPanels.open(OptionsPanels::kPanelSave);
		break;
	case MainMenu::kActionCredits:
		// The credits are a phase whose video ends by asking for the
		// menu again through the rule tables
		_mainMenu.close();
		gotoPhase(0x71);
		break;
	case MainMenu::kActionOptions:
		_optionsPanels.open(OptionsPanels::kPanelOptions);
		break;
	case MainMenu::kActionQuit:
		quitGame();
		break;
	default:
		break;
	}
}

void GamebotEngine::handleMouseClick(const Common::Point &screenPos) {
	// The main menu is modal above everything; its load, save and
	// options panels come first
	if (_mainMenu.isOpen()) {
		if (_optionsPanels.isOpen()) {
			// Loading and saving both drop back into the game
			if (_optionsPanels.handleClick(screenPos))
				_mainMenu.close();
			return;
		}
		runMenuAction(_mainMenu.handleClick(screenPos));
		return;
	}

	// An open conversation captures every click
	if (_logic.handleDialogClick(screenPos))
		return;

	// A click on a spoken line or a scripted animation skips it and
	// continues the chain
	if (_logic.skipCutscene())
		return;

	// Scripted sequences ignore every player input, as the original
	// does by disabling the mouse around them
	if (_logic.isBusy())
		return;

	// The inventory: picking an item hangs it from the cursor; the
	// inventory stays open and right click closes it. Clicking another
	// item while one hangs from the cursor combines the two.
	if (_inventoryUI.isOpen()) {
		uint32 item = _inventoryUI.handleClick(screenPos);
		if (item) {
			if (_linkedObject && _linkedObject != item) {
				uint32 linked = _linkedObject;
				linkObject(0);
				_logic.performVerb(item, kVerbUse, linked);
			} else {
				linkObject(item);
			}
		}
		return;
	}

	// The medallion switches control to the partner agent
	if (_changerBadge.handleClick(screenPos))
		return;

	Common::Point phasePos(screenPos.x + _world.origin().x, screenPos.y + _world.origin().y);
	HitResult hit;
	if (_world.hitTest(phasePos, hit)) {
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
		// Verbs go through the hold-to-select palette; a short click
		// on an object just walks towards it
	}

	debugC(kDebugEvents, "Click on floor at (%d,%d)", phasePos.x, phasePos.y);
	if (_linkedObject)
		linkObject(0);
	master().walkTo(_world, phasePos);
}

// Releasing the left button either picks the verb under the cursor
// (select mode) or acts as the click the original sends on button up
void GamebotEngine::handleLeftUp(const Common::Point &screenPos) {
	_leftDown = false;
	if (_verbPalette.isOpen()) {
		Verb verb;
		uint32 target = _verbPalette.targetObject();
		bool hitVerb = _verbPalette.handleClick(screenPos, verb);
		_verbPalette.close();
		if (hitVerb)
			_logic.interactWith(target, verb);
		return;
	}
	handleMouseClick(screenPos);
}

// The name of the hovered interactable, written exactly like any
// writer text (the original posts it as evTextWriteStr)
void GamebotEngine::drawHoverName(Graphics::Screen *screen) const {
	if (_hoverName.empty() || _logic.isBusy() || _logic.isDialogOpen() ||
			_inventoryUI.isOpen() || _verbPalette.isOpen() || _mainMenu.isOpen())
		return;
	_logic.writer().drawText(screen, _hoverName, false);
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
	// The cursor stays hidden while a video plays
	CursorMan.showMouse(false);
	_sounds.stopAll();
	if (soundCode)
		_sounds.playSound(soundCode);
	decoder.start();

	// Test harness hook: skip every video right away
	bool skipped = ConfMan.hasKey("gamebot_fastvideo") &&
		ConfMan.getBool("gamebot_fastvideo");
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
					event.type == Common::EVENT_RBUTTONDOWN ||
					(event.type == Common::EVENT_KEYDOWN &&
					event.kbd.keycode == Common::KEYCODE_ESCAPE))
				skipped = true;
		}
		g_system->delayMillis(10);
	}

	if (soundCode)
		_sounds.stopSound(soundCode);
	CursorMan.showMouse(true);
	// The original restore after a video fades from black into the
	// phase palette
	fadeIn(_world.palette());
	if (_world.musicCode())
		_sounds.playMusic(_world.musicCode());
	return true;
}

void GamebotEngine::fadeOut() {
	byte pal[256 * 3];
	g_system->getPaletteManager()->grabPalette(pal, 0, 256);
	bool pending = true;
	while (pending && !shouldQuit()) {
		pending = false;
		for (uint i = 0; i < sizeof(pal); i++) {
			if (pal[i]) {
				pal[i] -= MIN<byte>(5, pal[i]);
				pending = true;
			}
		}
		g_system->getPaletteManager()->setPalette(pal, 0, 256);
		_screen->update();
		g_system->delayMillis(10);
	}
}

void GamebotEngine::fadeIn(const byte *target) {
	byte pal[256 * 3] = {};
	g_system->getPaletteManager()->setPalette(pal, 0, 256);
	bool pending = true;
	while (pending && !shouldQuit()) {
		pending = false;
		for (uint i = 0; i < sizeof(pal); i++) {
			if (pal[i] < target[i]) {
				pal[i] += MIN<byte>(5, target[i] - pal[i]);
				pending = true;
			}
		}
		g_system->getPaletteManager()->setPalette(pal, 0, 256);
		_screen->update();
		g_system->delayMillis(10);
	}
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

Character *GamebotEngine::characterById(uint32 characterId) {
	Character *character =
		(characterId == _mortadelo.objectId()) ? &_mortadelo :
		(characterId == _filemon.objectId()) ? &_filemon :
		(characterId == _both.objectId()) ? &_both : nullptr;
	return (character && character->isLoaded()) ? character : nullptr;
}

void GamebotEngine::setMasterById(uint32 characterId) {
	Character *character = characterById(characterId);
	if (!character)
		return;
	_master->setTalking(false);
	_master = character;
	debugC(kDebugEvents, "Master is now %08x", characterId);
}

bool GamebotEngine::gotoPhase(uint32 phaseId) {
	// Every phase change fades the screen to black first, as the
	// original MainControl does before switching
	if (_world.currentPhaseId())
		fadeOut();
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
		// The phase data appoints the master: the original PostChange
		// posts a set-master with the first character entry
		Character *first =
			(phase.chars[0].characterId == _mortadelo.objectId()) ? &_mortadelo :
			(phase.chars[0].characterId == _filemon.objectId()) ? &_filemon :
			(phase.chars[0].characterId == _both.objectId()) ? &_both : nullptr;
		if (first && first->visible)
			_master = first;
		else if (_both.visible)
			_master = &_both;
		else if (!_master->visible && _mortadelo.visible)
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
				_lastMouseEventTime = g_system->getMillis();
				handleMouseMove(e.mouse);
				break;
			case Common::EVENT_LBUTTONDOWN:
				// Actions run on release; holding still 400 ms over an
				// interactable opens the verb palette instead
				_leftDown = true;
				_lastMouseEventTime = g_system->getMillis();
				break;
			case Common::EVENT_LBUTTONUP:
				handleLeftUp(e.mouse);
				break;
			case Common::EVENT_RBUTTONDOWN:
				// Right click also skips through scripted scenes;
				// otherwise it toggles the inventory safe
				if (_logic.skipCutscene())
					break;
				if (!_logic.isDialogOpen() && !_logic.isBusy() && !_mainMenu.isOpen()) {
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
				else if (e.kbd.keycode == Common::KEYCODE_TAB && !_logic.isBusy())
					switchMaster();
				else if (e.kbd.keycode == Common::KEYCODE_ESCAPE) {
					if (_optionsPanels.isOpen())
						_optionsPanels.close();
					else if (_mainMenu.isOpen())
						_mainMenu.close();
					else
						_mainMenu.open();
				}
				break;
			default:
				break;
			}
		}

		uint32 millis = g_system->getMillis();
		// The cursor vanishes while a scripted sequence runs
		if (CursorMan.isVisible() == _logic.isBusy())
			CursorMan.showMouse(!_logic.isBusy());

		// Original select mode: the left button held still for 400 ms
		// over a hot object pops the verb palette under the cursor
		if (_leftDown && !_verbPalette.isOpen() && _hoverObjectId &&
				!_linkedObject && millis - _lastMouseEventTime >= 400 &&
				!_logic.isBusy() && !_logic.isDialogOpen() &&
				!_mainMenu.isOpen() && !_inventoryUI.isOpen()) {
			Common::Point mouse = g_system->getEventManager()->getMousePos();
			_verbPalette.open(mouse, _hoverObjectId);
			_verbPalette.updateHover(mouse);
		}
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
		drawHoverName(_screen);
		_logic.writer().draw(_screen);
		_logic.drawDialog(_screen);
		_changerBadge.draw(_screen);
		_verbPalette.draw(_screen);
		_inventoryUI.draw(_screen);
		_mainMenu.draw(_screen);
		_optionsPanels.draw(_screen);
		limiter.delayBeforeSwap();
		_screen->update();
		limiter.startFrame();
	}

	return Common::kNoError;
}

Common::Error GamebotEngine::syncGame(Common::Serializer &s) {
	if (!s.syncVersion(2))
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

	// Since version 2 every character carries its own state, so a
	// restore brings back the partner and the changer medallion
	Character *all[3] = { &_mortadelo, &_filemon, &_both };
	int16 cx[3], cy[3];
	uint16 corient[3], clayer[3];
	byte cvisible[3];
	if (s.getVersion() >= 2) {
		for (uint i = 0; i < 3; i++) {
			cx[i] = all[i]->x(); cy[i] = all[i]->y();
			corient[i] = all[i]->orientation(); clayer[i] = all[i]->layer();
			cvisible[i] = all[i]->visible ? 1 : 0;
			s.syncAsSint16LE(cx[i]);
			s.syncAsSint16LE(cy[i]);
			s.syncAsUint16LE(corient[i]);
			s.syncAsUint16LE(clayer[i]);
			s.syncAsByte(cvisible[i]);
		}
	}

	if (s.isLoading()) {
		if (!_world.gotoPhase(phaseId))
			return Common::kUnknownError;
		_logic.applyObjectStates();
		// Characters place themselves once the phase (and its walk
		// map, which drives the scale bands) is loaded
		if (s.getVersion() >= 2) {
			for (uint i = 0; i < 3; i++) {
				CharacterLocation location;
				location.characterId = all[i]->objectId();
				location.x = cx[i];
				location.y = cy[i];
				location.orientation = corient[i];
				location.layer = clayer[i];
				all[i]->enterPhase(_world, location);
				all[i]->visible = cvisible[i] != 0;
			}
		}
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
