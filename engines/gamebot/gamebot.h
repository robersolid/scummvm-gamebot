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

#ifndef GAMEBOT_H
#define GAMEBOT_H

#include "common/error.h"
#include "common/random.h"
#include "common/serializer.h"
#include "common/system.h"
#include "engines/engine.h"
#include "graphics/screen.h"

#include "gamebot/character.h"
#include "gamebot/detection.h"
#include "gamebot/logic.h"
#include "gamebot/resource.h"
#include "gamebot/sound.h"
#include "gamebot/ui.h"
#include "gamebot/world.h"

namespace Gamebot {

// The original engine runs at a fixed 640x480x8 video mode
constexpr int16 kScreenWidth = 640;
constexpr int16 kScreenHeight = 480;

// Data file names, as hardcoded in the original MainClass.h
#define GAMEBOT_RESOURCE_FILE "Resdata.res"
#define GAMEBOT_ACTIONS_FILE "Actions.act"
#define GAMEBOT_NEWGAME_FILE "default.def"
#define GAMEBOT_NEWGAME_DIALOG_FILE "default.dlg"

class Console;

class GamebotEngine : public Engine {
private:
	const ADGameDescription *_gameDescription;
	Common::RandomSource _randomSource;

	ResourceFile _resources;    // Resdata.res: graphics, sounds, phase data
	ResourceFile _dialogFile;   // default.dlg: initial dialog states
	ActionFile _actions;        // Actions.act: per-object action rules
	WorldFile _initialWorld;    // default.def: world state for a new game
	World _world;               // runtime world (current phase)
	// The three playable characters: Mortadelo, Filemon and the
	// combined "both" sprite used on the map screens. The master is
	// the one the player controls.
	Character _mortadelo, _filemon, _both;
	Character *_master = &_mortadelo;
	Logic _logic;               // action rules, inventory, phrases
	VerbPalette _verbPalette;   // pop-up verb selection
	InventoryUI _inventoryUI;   // the safe with the collected objects
	ChangerBadge _changerBadge; // the character-switch medallion
	MainMenu _mainMenu;         // the original main menu panel
	OptionsPanels _optionsPanels; // load/save/options screens
	SoundManager _sounds;       // ADPCM voices, effects and music
	Common::Point _lastMousePhasePos; // for the in-bounds conditions
	Common::String _hoverName;        // interactable name under the cursor
	uint32 _linkedObject = 0;   // inventory object selected for use-with

	// Loads the data file indexes. Returns false if a mandatory file
	// is missing or corrupt.
	bool loadDataFiles();

	// Mouse cursors (aMouseImage resources of the MouseSys object)
	struct Cursor {
		byte *pixels = nullptr;
		uint16 width = 0, height = 0;
		uint16 hotX = 0, hotY = 0;
	};
	Cursor _standardCursor, _hotCursor;
	// Every mouse image of the original driver, loaded on demand:
	// standard, hot, selecting, the four exit arrows and the click
	Common::HashMap<uint32, Cursor> _cursorCache;
	uint32 _hoverCursorRes = 0x00030001;
	uint32 _appliedCursorRes = 0;
	const Cursor *cursorFor(uint32 resId);
	void updateCursorImage();
	bool _hotCursorShown = false;
	uint32 _hoverObjectId = 0;
	// Left-button hold tracking for the verb palette (the original
	// enters select mode after 400 ms without mouse events)
	bool _leftDown = false;
	uint32 _lastMouseEventTime = 0;
	bool _gameStarted = false;

	void loadCursor(uint32 resId, Cursor &cursor);
	void setCursor(const Cursor &cursor);
	void applyLinkedCursor(bool highlighted);

public:
	void handleMouseMove(const Common::Point &screenPos);
	void handleMouseClick(const Common::Point &screenPos);
	// The verb palette opens after holding the left button 400 ms over
	// an interactable (the original MouseSys select mode); pointing and
	// releasing runs the verb
	void handleLeftUp(const Common::Point &screenPos);

protected:
	// Engine APIs
	Common::Error run() override;

public:
	Graphics::Screen *_screen = nullptr;

	GamebotEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~GamebotEngine() override;

	uint32 getFeatures() const;
	Common::String getGameId() const;

	ResourceFile &resources() { return _resources; }
	ResourceFile &dialogFile() { return _dialogFile; }
	ActionFile &actions() { return _actions; }
	WorldFile &initialWorld() { return _initialWorld; }
	World &world() { return _world; }
	Character &mortadelo() { return _mortadelo; }
	Character &filemon() { return _filemon; }
	Character &master() { return *_master; }
	bool isGameStarted() const { return _gameStarted; }
	const Character *secondCharacter() const {
		// The visible non-master partner, if any
		if (_master != &_mortadelo && _mortadelo.visible && _mortadelo.isLoaded())
			return &_mortadelo;
		if (_master != &_filemon && _filemon.visible && _filemon.isLoaded())
			return &_filemon;
		return nullptr;
	}
	void switchMaster();
	void setMasterById(uint32 characterId);
	// The character owning an id, or null if none/not loaded
	Character *characterById(uint32 characterId);
	Logic &logic() { return _logic; }
	VerbPalette &verbPalette() { return _verbPalette; }
	InventoryUI &inventoryUI() { return _inventoryUI; }
	ChangerBadge &changerBadge() { return _changerBadge; }
	MainMenu &mainMenu() { return _mainMenu; }
	OptionsPanels &optionsPanels() { return _optionsPanels; }
	const Common::String &targetName() const { return _targetName; }
	SoundManager &sounds() { return _sounds; }
	const Common::Point &lastMousePhasePos() const { return _lastMousePhasePos; }
	void drawHoverName(Graphics::Screen *screen) const;
	void runMenuAction(int action);
	// Selects an inventory object for use-with, swapping the cursor
	// for its image; 0 restores the standard cursor
	void linkObject(uint32 objectId);
	uint32 linkedObject() const { return _linkedObject; }

	// Loads a phase and places the characters at its entry positions
	bool gotoPhase(uint32 phaseId);

	// Palette fades of the original VideoSys: every component ramps
	// by 5 every 10 ms, down to black or up from black to the target
	void fadeOut();
	void fadeIn(const byte *target);

	// Plays a full-screen FLC video (modal, skippable with a click
	// or escape), then restores the phase palette and music
	bool playVideo(uint32 flicResId);

	uint32 getRandomNumber(uint maxNum) {
		return _randomSource.getRandomNumber(maxNum);
	}

	bool hasFeature(EngineFeature f) const override {
		return
			(f == kSupportsLoadingDuringRuntime) ||
			(f == kSupportsSavingDuringRuntime) ||
			(f == kSupportsReturnToLauncher);
	};

	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override {
		return true;
	}
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override {
		return true;
	}

	Common::Error syncGame(Common::Serializer &s);

	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override {
		Common::Serializer s(nullptr, stream);
		return syncGame(s);
	}
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override {
		Common::Serializer s(stream, nullptr);
		Common::Error err = syncGame(s);
		if (err.getCode() == Common::kNoError)
			_gameStarted = true;
		return err;
	}
};

extern GamebotEngine *g_engine;
#define SHOULD_QUIT ::Gamebot::g_engine->shouldQuit()

} // End of namespace Gamebot

#endif // GAMEBOT_H
