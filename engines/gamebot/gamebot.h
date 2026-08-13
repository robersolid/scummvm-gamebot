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
	Character _mortadelo;       // the player character (master)
	Logic _logic;               // action rules, inventory, phrases

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
	bool _hotCursorShown = false;
	uint32 _hoverObjectId = 0;

	void loadCursor(uint32 resId, Cursor &cursor);
	void setCursor(const Cursor &cursor);
	void handleMouseMove(const Common::Point &screenPos);
	void handleMouseClick(const Common::Point &screenPos);

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
	Logic &logic() { return _logic; }

	// Loads a phase and places the characters at its entry positions
	bool gotoPhase(uint32 phaseId);

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
		return syncGame(s);
	}
};

extern GamebotEngine *g_engine;
#define SHOULD_QUIT ::Gamebot::g_engine->shouldQuit()

} // End of namespace Gamebot

#endif // GAMEBOT_H
