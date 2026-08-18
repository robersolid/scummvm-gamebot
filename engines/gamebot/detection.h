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

#ifndef GAMEBOT_DETECTION_H
#define GAMEBOT_DETECTION_H

#include "engines/advancedDetector.h"

namespace Gamebot {

enum GamebotDebugChannels {
	kDebugEvents = 1,
	kDebugActions,
	kDebugResources,
	kDebugWalk,
	kDebugSound,
	kDebugSaves,
};

#define GAMEOPTION_ALLOW_SKIP GUIO_GAMEOPTIONS1

extern const PlainGameDescriptor gamebotGames[];

extern const ADGameDescription gameDescriptions[];

} // End of namespace Gamebot

class GamebotMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
	static const DebugChannelDef debugFlagList[];

public:
	GamebotMetaEngineDetection();
	~GamebotMetaEngineDetection() override {}

	const char *getName() const override {
		return "gamebot";
	}

	const char *getEngineName() const override {
		return "GameBot";
	}

	const char *getOriginalCopyright() const override {
		return "GameBot (C) 1999 Eridani S.L.";
	}

	const DebugChannelDef *getDebugChannels() const override {
		return debugFlagList;
	}
};

#endif // GAMEBOT_DETECTION_H
