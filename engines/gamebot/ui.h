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
	// True while the point stays inside the open palette
	bool contains(const Common::Point &screenPos) const;
	// Returns the picked verb through outVerb; false = closed with no pick
	bool handleClick(const Common::Point &screenPos, Verb &outVerb);
	void draw(Graphics::Screen *screen) const;

private:
	struct Image {
		Common::Rect rect;      // in palette-resource coordinates
		byte *pixels = nullptr;
	};

	int hitIcon(const Common::Point &screenPos) const;

	// One palette design per playable character, as the original
	// swaps the select image on every master change
	struct Set {
		Image background;
		Image icons[4];
		uint32 actionCodes[4] = {};
	};
	bool loadSet(uint32 resId, Set &set);
	const Set &activeSet() const;

	Set _sets[4]; // Mortadelo, Filemon, and their Super variants
	bool _loaded = false;
	bool _open = false;
	Common::Point _pos;         // top-left of the palette on screen
	uint32 _objectId = 0;
	int _hover = -1;
};

// The original main menu (OptionsMaster): a full-screen panel with
// its own palette and image buttons. The load, save and options
// screens delegate to the ScummVM dialogs.
class MainMenu {
public:
	enum Action {
		kActionNone = -1,
		kActionNewGame = 0,
		kActionLoad,
		kActionSave,
		kActionCredits,
		kActionOptions,
		kActionQuit,
		kActionReturn,
		kActionCount
	};

	~MainMenu();

	bool load();
	void open();
	void close();
	bool isOpen() const { return _open; }

	void updateHover(const Common::Point &screenPos);
	Action handleClick(const Common::Point &screenPos);
	void draw(Graphics::Screen *screen) const;

private:
	struct Image {
		Common::Rect rect;
		byte *pixels = nullptr;
	};

	bool loadImage(uint32 objectId, uint imageIndex, Image &image);
	int hitButton(const Common::Point &screenPos) const;

	Image _background;
	Image _buttons[kActionCount];     // normal state
	Image _highlights[kActionCount];  // hovered state
	byte _palette[256 * 3] = {};
	bool _loaded = false;
	bool _open = false;
	int _hover = -1;
};

// The medallion of the original ChangerMaster: a badge at the top
// right showing the partner agent; clicking it spins the badge and
// hands control to the other character.
class ChangerBadge {
public:
	~ChangerBadge();

	bool load();
	// True when the click hit the badge (and the spin started)
	bool handleClick(const Common::Point &screenPos);
	void update(uint32 millis);
	void draw(Graphics::Screen *screen);

	static const int16 kBadgeWidth = 80, kBadgeHeight = 78;

private:
	struct Face {
		byte *pixels = nullptr; // kBadgeWidth * kBadgeHeight
	};
	struct Spin {
		byte *frames = nullptr;
		uint32 frameSize = 0;
		uint32 imageCount = 0;
		uint32 framePeriod = 0;
		Common::Array<uint32> sequence;
	};

	int16 badgeX() const; // badge lives at the top-right corner

	Face _faces[4];  // Mortadelo, Filemon, and their grayed variants
	Spin _spins[2];
	bool _loaded = false;

	bool _spinning = false;
	uint _spinIndex = 0;
	uint32 _spinStep = 0;
	uint32 _spinTime = 0;
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

	// Inventory image of an item, for the object-as-cursor swap
	const byte *itemCursor(uint32 objectId, int16 &width, int16 &height, bool highlighted = false);

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

public:
	uint32 hoverObject() const { return _open ? _hoverObject : 0; }
};

// The load, save and options screens of the original OptionsMaster:
// full-screen panels over the main menu with three-state buttons, ten
// save slot lines showing the save dates, and six-level volume bars
class OptionsPanels {
public:
	enum Panel { kPanelNone, kPanelLoad, kPanelSave, kPanelOptions };

	~OptionsPanels();

	void open(Panel panel);
	void close() { _panel = kPanelNone; }
	bool isOpen() const { return _panel != kPanelNone; }

	void updateHover(const Common::Point &screenPos);
	// Returns true when the click closed the whole menu (a game load)
	bool handleClick(const Common::Point &screenPos);
	void draw(Graphics::Screen *screen);

private:
	struct Image {
		Common::Rect rect;
		byte *pixels = nullptr;
	};
	struct Button {
		Image states[3]; // normal, hot, pressed
	};

	static const uint kSlotCount = 10;
	static const uint kBarLevels = 6;

	bool load();
	bool loadImage(uint32 objectId, uint imageIndex, Image &image) const;
	void loadButton(uint32 objectId, Button &button) const;
	void refreshSaves();
	void drawButton(Graphics::Screen *screen, const Button &button, bool hot) const;
	int hitImage(const Common::Point &screenPos, const Image &image) const;
	int volumeLevel(uint bar) const;
	void setVolumeLevel(uint bar, int level);

	bool _loaded = false;
	Panel _panel = kPanelNone;

	Image _loadBg, _saveBg, _optionsBg;
	Button _loadAction, _loadBack;
	Button _saveAction, _saveBack;
	Image _loadSlots[kSlotCount]; // marked-line image per slot
	Image _saveSlots[kSlotCount];
	Image _bars[3][kBarLevels];   // effects, music, voices
	Button _barMore[3], _barLess[3];
	Button _optionsBack;

	int _marked = -1;             // marked save slot line
	int _hover = -1;              // hovered button (panel-local index)
	Common::String _slotText[kSlotCount]; // save dates
	bool _slotUsed[kSlotCount] = {};
};

} // End of namespace Gamebot

#endif // GAMEBOT_UI_H
