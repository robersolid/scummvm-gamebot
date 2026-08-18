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

#include "gamebot/metaengine.h"
#include "gamebot/detection.h"
#include "gamebot/gamebot.h"
#include "common/translation.h"

namespace Gamebot {

static const ADExtraGuiOptionsMap optionsList[] = {
	{
		GAMEOPTION_ALLOW_SKIP,
		{
			_s("Allow skipping cutscenes, dialogs and videos"),
			_s("Enable skipping of videos, animations, and spoken lines with Escape, Space, or clicks (disabled by default for original game behavior)"),
			"allow_skip",
			false,
			0,
			0
		}
	},
	AD_EXTRA_GUI_OPTIONS_TERMINATOR
};

} // End of namespace Gamebot

const char *GamebotMetaEngine::getName() const {
	return "gamebot";
}

const ADExtraGuiOptionsMap *GamebotMetaEngine::getAdvancedExtraGuiOptions() const {
	return Gamebot::optionsList;
}

Common::Error GamebotMetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const {
	*engine = new Gamebot::GamebotEngine(syst, desc);
	return Common::kNoError;
}

bool GamebotMetaEngine::hasFeature(MetaEngineFeature f) const {
	return checkExtendedSaves(f) ||
		(f == kSupportsLoadingDuringStartup);
}

#if PLUGIN_ENABLED_DYNAMIC(GAMEBOT)
REGISTER_PLUGIN_DYNAMIC(GAMEBOT, PLUGIN_TYPE_ENGINE, GamebotMetaEngine);
#else
REGISTER_PLUGIN_STATIC(GAMEBOT, PLUGIN_TYPE_ENGINE, GamebotMetaEngine);
#endif
