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

#ifndef GAMEBOT_UI_H
#define GAMEBOT_UI_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/rect.h"

#include "gamebot/logic.h"
#include "gamebot/resource.h"

namespace Graphics {
class Screen;
}

namespace Gamebot {

// The verb palette (original ResSelectImage): a background image and
// up to four verb icons, each mapping to a mouse action mode. It pops
// up centered on the clicked object; picking an icon performs that
// verb on the object.
class VerbPalette {
public:
	~VerbPalette();

	bool load();
	void open(const Common::Point &screenPos, uint32 objectId);
	void close() { _open = false; }
	bool isOpen() const { return _open; }
	uint32 targetObject() const { return _objectId; }

	void updateHover(const Common::Point &screenPos);
	// Returns the picked verb through outVerb; false = closed with no pick
	bool handleClick(const Common::Point &screenPos, Verb &outVerb);
	void draw(Graphics::Screen *screen) const;

private:
	struct Image {
		Common::Rect rect;      // in palette-resource coordinates
		byte *pixels = nullptr;
	};

	int hitIcon(const Common::Point &screenPos) const;

	Image _background;
	Image _icons[4];
	uint32 _actionCodes[4] = {};
	bool _loaded = false;
	bool _open = false;
	Common::Point _pos;         // top-left of the palette on screen
	uint32 _objectId = 0;
	int _hover = -1;
};

// The inventory of this game is an open safe: right click shows it
// with the collected objects laid out in a grid; clicking one selects
// it for a use-with interaction.
class InventoryUI {
public:
	~InventoryUI();

	bool load();
	void toggle();
	void close() { _open = false; }
	bool isOpen() const { return _open; }

	void updateHover(const Common::Point &screenPos);
	// Returns the picked object id, or 0 when nothing was hit
	uint32 handleClick(const Common::Point &screenPos);
	void draw(Graphics::Screen *screen);

private:
	struct ItemImages {
		Common::Rect rect;
		byte *normal = nullptr;
		byte *highlight = nullptr;
	};

	// Original layout constants (InventMaster.cpp)
	static const int16 kSlotWidth = 80, kSlotHeight = 46;
	static const int16 kSeparationX = 15, kSeparationY = 12;
	// Item area inside the safe: (70,126)-(434,346)
	static const int16 kAreaLeft = 70, kAreaTop = 126;
	static const int16 kAreaRight = 434, kAreaBottom = 346;

	struct Image {
		Common::Rect rect;
		byte *pixels = nullptr;
	};

	const ItemImages *itemImages(uint32 objectId);
	int slotAt(const Common::Point &screenPos) const;
	Common::Array<uint32> itemsInOrder() const;

	Image _safeImage;
	Common::HashMap<uint32, ItemImages> _itemCache;
	bool _loaded = false;
	bool _open = false;
	uint32 _hoverObject = 0;
};

} // End of namespace Gamebot

#endif // GAMEBOT_UI_H
