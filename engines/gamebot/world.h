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

#ifndef GAMEBOT_WORLD_H
#define GAMEBOT_WORLD_H

#include "common/array.h"
#include "common/rect.h"

#include "gamebot/resource.h"

namespace Graphics {
class Screen;
}

namespace Gamebot {

// Width in pixels of a walk map cell (rows are bands delimited by the
// per-row baseY values); from the original Character.cpp
constexpr int kWalkCellWidth = 30;

// Runtime world: currently a single loaded phase rendered statically.
// Layer 0 of every phase holds the disabled objects and is never drawn;
// the other layers are drawn from the last one (background) down to 1.
class World {
public:
	~World() { clear(); }

	bool gotoPhase(uint32 phaseId);
	void draw(Graphics::Screen *screen);

	uint32 currentPhaseId() const { return _phaseId; }
	int16 phaseWidth() const { return _phaseWidth; }
	int16 phaseHeight() const { return _phaseHeight; }
	Common::Point &origin() { return _origin; }

	// Debug overlay toggles, registered as console variables
	bool _showWalkMap = false;
	bool _showHotspots = false;

private:
	struct DrawItem {
		uint32 objectId = 0;
		uint32 resId = 0;
		Common::Rect rect;      // absolute phase coordinates
		byte *pixels = nullptr; // owned; rect.width() * rect.height()
	};

	struct Hotspot {
		uint32 objectId = 0;
		ResourceType type = kResUnknown;
		Common::Rect rect;
	};

	void clear();
	bool loadPhaseInit(uint32 phaseId);
	void loadWalkMap(uint32 phaseId);
	void addDrawItem(const ObjectEntry &object);
	void drawWalkMapOverlay(Graphics::Screen *screen) const;
	void drawHotspotOverlay(Graphics::Screen *screen) const;

	uint32 _phaseId = 0;
	int16 _phaseWidth = 0, _phaseHeight = 0;
	Common::Point _origin; // scroll offset of the visible window

	Common::Array<DrawItem> _items; // in back-to-front draw order
	Common::Array<Hotspot> _hotspots;

	// Walk map of the current phase (empty if it has none)
	uint32 _mapWidth = 0, _mapHeight = 0;
	Common::Array<byte> _mapCells;
	Common::Array<uint16> _mapBaseY;
	Common::Array<uint16> _mapScale;
};

} // End of namespace Gamebot

#endif // GAMEBOT_WORLD_H
