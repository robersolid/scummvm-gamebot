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

#ifndef GAMEBOT_DEBUG_NAMES_H
#define GAMEBOT_DEBUG_NAMES_H

#include "common/str.h"

#include "gamebot/resource.h"

namespace Gamebot {

// Human-readable names for codes of the original engine, used by debug
// traces and console commands. They return nullptr for unknown codes
// (except conditionName, which formats unknown codes inline).

const char *eventName(uint32 code);
const char *actionName(uint32 code);
Common::String conditionName(uint16 code);
const char *resourceTypeName(ResourceType type);

} // End of namespace Gamebot

#endif // GAMEBOT_DEBUG_NAMES_H
