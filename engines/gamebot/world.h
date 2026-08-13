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
#include "common/str.h"

#include "gamebot/resource.h"

namespace Graphics {
class Screen;
}

namespace Gamebot {

// Width in pixels of a walk map cell (rows are bands delimited by the
// per-row baseY values); from the original Character.cpp
constexpr int kWalkCellWidth = 30;

// Result of a hit test against the current phase
struct HitResult {
	uint32 objectId = 0;
	Common::String name;
	ResourceType type = kResUnknown;  // resource that matched
	uint32 exitPhase = 0;             // destination when hitting an exit
};

// Runtime world: currently a single loaded phase with running
// automatic animations. Layer 0 of every phase holds the disabled
// objects and is never drawn; the other layers are drawn from the
// last one (background) down to 1.
class World {
public:
	~World() { clear(); }

	bool gotoPhase(uint32 phaseId);
	void update(uint32 millis); // advances animations
	uint animatedCount() const {
		uint n = 0;
		for (uint i = 0; i < _items.size(); i++)
			if (!_items[i].anims.empty())
				n++;
		return n;
	}
	void draw(Graphics::Screen *screen);

	// Pixel-perfect test in phase coordinates, front to back
	bool hitTest(const Common::Point &pos, HitResult &result) const;

	uint32 currentPhaseId() const { return _phaseId; }
	int16 phaseWidth() const { return _phaseWidth; }
	int16 phaseHeight() const { return _phaseHeight; }
	Common::Point &origin() { return _origin; }

	// Debug overlay toggles
	bool _showWalkMap = false;
	bool _showHotspots = false;

private:
	// One automatic animation of an object. Every auto animation of an
	// object runs its own clock in the original engine: after its start
	// pause it becomes the object's active resource and plays.
	struct Animation {
		uint32 resId = 0;
		Common::Rect rect;       // frame rect, absolute phase coordinates
		byte *frames = nullptr;  // owned; all frames, frameSize each
		uint32 frameSize = 0;
		AnimationParams params;
		Common::Array<SequenceStep> sequence;
		uint32 seqPos = 0;
		uint32 fireTime = 0;     // when this animation (re)starts
		uint32 stepTime = 0;     // when to advance while running
		int16 stepDeltaX = 0, stepDeltaY = 0; // per-step displacement (mobile)
		bool running = false;
	};

	struct DrawItem {
		uint32 objectId = 0;
		Common::String name;
		Common::Rect rect;       // static image rect, absolute phase coords
		byte *staticPixels = nullptr; // owned; may be null if animation-only

		Common::Array<Animation> anims;
		int activeAnim = -1;     // index into anims, -1 = static image
		int16 curDeltaX = 0, curDeltaY = 0;
		bool visible = true;

		const byte *currentPixels() const {
			if (activeAnim >= 0) {
				const Animation &a = anims[activeAnim];
				uint32 imageIndex = a.sequence.empty() ? 1 : a.sequence[a.seqPos].imageIndex;
				if (imageIndex < 1 || imageIndex > a.params.imageCount)
					imageIndex = 1;
				return a.frames + (imageIndex - 1) * a.frameSize;
			}
			return staticPixels;
		}
		Common::Rect currentRect() const {
			Common::Rect r = (activeAnim >= 0) ? anims[activeAnim].rect : rect;
			r.translate(curDeltaX, curDeltaY);
			return r;
		}
	};

	struct Hotspot {
		uint32 objectId = 0;
		Common::String name;
		ResourceType type = kResUnknown;
		Common::Rect rect;
		uint32 exitPhase = 0;    // for phase/map exits
	};

	void clear();
	bool loadPhaseInit(uint32 phaseId);
	void loadWalkMap(uint32 phaseId);
	void addDrawItem(const ObjectEntry &object);
	bool loadAnimation(const ResourceEntry &e, Animation &anim);
	void updateItem(DrawItem &item, uint32 millis);
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
