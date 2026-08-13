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

#include "common/debug.h"
#include "common/endian.h"
#include "graphics/screen.h"

#include "gamebot/gamebot.h"
#include "gamebot/ui.h"

namespace Gamebot {

static const byte kTransparentColor = 0;

// Verb palette resource of Mortadelo (original ResSelectMortaNormal)
static const uint32 kPaletteResId = 0x00031001;
// Mouse mode action codes carried by the palette icons
enum {
	kModePickup = 0x00010000,
	kModeTalk = 0x00020000,
	kModeView = 0x00040000,
	kModeOpen = 0x00080000
};

static void blitImage(Graphics::Screen *screen, const byte *pixels,
		int16 x, int16 y, int16 w, int16 h) {
	Common::Rect dest(x, y, x + w, y + h);
	Common::Rect clipped(dest);
	clipped.clip(Common::Rect(0, 0, screen->w, screen->h));
	for (int dy = clipped.top; dy < clipped.bottom; dy++) {
		const byte *src = pixels + (dy - dest.top) * w + (clipped.left - dest.left);
		byte *dst = (byte *)screen->getBasePtr(clipped.left, dy);
		for (int dx = clipped.width(); dx > 0; dx--, src++, dst++) {
			if (*src != kTransparentColor)
				*dst = *src;
		}
	}
}

VerbPalette::~VerbPalette() {
	delete[] _background.pixels;
	for (uint i = 0; i < 4; i++)
		delete[] _icons[i].pixels;
}

// Layout of a select-image blob: 5 rects, 4 action codes, then the
// pixel data of every rect whose bottom coordinate is not zero. The
// first rect/image is the palette background, the rest are the icons.
bool VerbPalette::load() {
	ResourceFile &res = g_engine->resources();
	const ResourceEntry *e = res.findByResId(kPaletteResId);
	if (!e)
		e = res.findResource(3 /* MouseSys */, kResSelectImage);
	if (!e) {
		warning("Verb palette resource not found");
		return false;
	}

	byte *data = res.readBlob(*e);
	if (!data)
		return false;

	Common::Rect rects[5];
	for (uint i = 0; i < 5; i++) {
		const byte *p = data + i * 16;
		rects[i] = Common::Rect(
			READ_LE_INT32(p), READ_LE_INT32(p + 4),
			READ_LE_INT32(p + 8) + 1, READ_LE_INT32(p + 12) + 1);
	}
	for (uint i = 0; i < 4; i++)
		_actionCodes[i] = READ_LE_UINT32(data + 5 * 16 + i * 4);

	uint32 pos = 5 * 16 + 4 * 4;
	for (uint i = 0; i < 5; i++) {
		// Presence marker: a zero bottom coordinate means no image
		if (READ_LE_INT32(data + i * 16 + 12) == 0)
			continue;
		Image &image = (i == 0) ? _background : _icons[i - 1];
		image.rect = rects[i];
		uint32 size = rects[i].width() * rects[i].height();
		image.pixels = new byte[size];
		memcpy(image.pixels, data + pos, size);
		pos += size;
	}
	delete[] data;

	_loaded = _background.pixels != nullptr;
	debugC(kDebugResources, "Verb palette loaded (%dx%d)",
		_background.rect.width(), _background.rect.height());
	return _loaded;
}

void VerbPalette::open(const Common::Point &screenPos, uint32 objectId) {
	if (!_loaded && !load())
		return;
	_objectId = objectId;
	_pos.x = CLIP<int16>(screenPos.x - _background.rect.width() / 2,
		0, kScreenWidth - _background.rect.width());
	_pos.y = CLIP<int16>(screenPos.y - _background.rect.height() / 2,
		0, kScreenHeight - _background.rect.height());
	_hover = -1;
	_open = true;
}

// Icon rects share the coordinate space of the background rect
int VerbPalette::hitIcon(const Common::Point &screenPos) const {
	Common::Point local(screenPos.x - _pos.x + _background.rect.left,
		screenPos.y - _pos.y + _background.rect.top);
	for (uint i = 0; i < 4; i++) {
		if (!_icons[i].pixels || !_icons[i].rect.contains(local))
			continue;
		byte pixel = _icons[i].pixels[
			(local.y - _icons[i].rect.top) * _icons[i].rect.width() +
			(local.x - _icons[i].rect.left)];
		if (pixel != kTransparentColor)
			return (int)i;
	}
	return -1;
}

void VerbPalette::updateHover(const Common::Point &screenPos) {
	if (_open)
		_hover = hitIcon(screenPos);
}

bool VerbPalette::handleClick(const Common::Point &screenPos, Verb &outVerb) {
	int icon = hitIcon(screenPos);
	_open = false;
	if (icon < 0)
		return false;

	switch (_actionCodes[icon]) {
	case kModePickup: outVerb = kVerbTake; break;
	case kModeTalk: outVerb = kVerbTalk; break;
	case kModeView: outVerb = kVerbLook; break;
	case kModeOpen: outVerb = kVerbOpen; break;
	default: return false;
	}
	return true;
}

void VerbPalette::draw(Graphics::Screen *screen) const {
	if (!_open || !_loaded)
		return;
	blitImage(screen, _background.pixels, _pos.x, _pos.y,
		_background.rect.width(), _background.rect.height());
	// The hovered icon is drawn highlighted over the background
	if (_hover >= 0 && _icons[_hover].pixels) {
		const Image &icon = _icons[_hover];
		blitImage(screen, icon.pixels,
			_pos.x + icon.rect.left - _background.rect.left,
			_pos.y + icon.rect.top - _background.rect.top,
			icon.rect.width(), icon.rect.height());
	}
}

InventoryUI::~InventoryUI() {
	delete[] _safeImage.pixels;
	for (auto &entry : _itemCache) {
		delete[] entry._value.normal;
		delete[] entry._value.highlight;
	}
}

// The inventory background is the safe image of the InventoryMaster
// object (id 0x12)
bool InventoryUI::load() {
	ResourceFile &res = g_engine->resources();
	const ResourceEntry *e = res.findResource(0x12, kResImage);
	if (!e) {
		warning("Inventory background not found");
		return false;
	}
	byte *data = res.readBlob(*e);
	if (!data)
		return false;
	_safeImage.rect = Common::Rect(
		READ_LE_INT32(data), READ_LE_INT32(data + 4),
		READ_LE_INT32(data + 8) + 1, READ_LE_INT32(data + 12) + 1);
	uint32 size = _safeImage.rect.width() * _safeImage.rect.height();
	_safeImage.pixels = new byte[size];
	memcpy(_safeImage.pixels, data + 16, size);
	delete[] data;
	_loaded = true;
	return true;
}

void InventoryUI::toggle() {
	if (!_loaded && !load())
		return;
	_open = !_open;
	_hoverObject = 0;
}

// Inventory images hold two pictures: normal and highlighted
const InventoryUI::ItemImages *InventoryUI::itemImages(uint32 objectId) {
	if (_itemCache.contains(objectId))
		return &_itemCache[objectId];

	ResourceFile &res = g_engine->resources();
	const ResourceEntry *e = res.findResource(objectId, kResInventoryImage);
	if (!e)
		return nullptr;
	byte *data = res.readBlob(*e);
	if (!data)
		return nullptr;

	ItemImages images;
	images.rect = Common::Rect(
		READ_LE_INT32(data), READ_LE_INT32(data + 4),
		READ_LE_INT32(data + 8) + 1, READ_LE_INT32(data + 12) + 1);
	uint32 size = images.rect.width() * images.rect.height();
	images.normal = new byte[size];
	images.highlight = new byte[size];
	memcpy(images.normal, data + 16, size);
	memcpy(images.highlight, data + 16 + size, size);
	delete[] data;
	_itemCache[objectId] = images;
	return &_itemCache[objectId];
}

Common::Array<uint32> InventoryUI::itemsInOrder() const {
	Common::Array<uint32> items;
	for (auto &entry : g_engine->logic().inventory())
		items.push_back(entry._key);
	return items;
}

// Items flow left to right, top to bottom in fixed-size slots
// (original InventMaster layout)
int InventoryUI::slotAt(const Common::Point &screenPos) const {
	if (screenPos.x < kAreaLeft || screenPos.y < kAreaTop ||
			screenPos.x >= kAreaRight || screenPos.y >= kAreaBottom)
		return -1;
	int perRow = 1;
	while ((kSlotWidth * (perRow + 1) + kSeparationX * perRow) <= (kAreaRight - kAreaLeft))
		perRow++;
	int column = (screenPos.x - kAreaLeft) / (kSlotWidth + kSeparationX);
	int row = (screenPos.y - kAreaTop) / (kSlotHeight + kSeparationY);
	if (column >= perRow)
		return -1;
	return row * perRow + column;
}

void InventoryUI::updateHover(const Common::Point &screenPos) {
	if (!_open)
		return;
	int slot = slotAt(screenPos);
	Common::Array<uint32> items = itemsInOrder();
	_hoverObject = (slot >= 0 && slot < (int)items.size()) ? items[slot] : 0;
}

uint32 InventoryUI::handleClick(const Common::Point &screenPos) {
	int slot = slotAt(screenPos);
	Common::Array<uint32> items = itemsInOrder();
	if (slot >= 0 && slot < (int)items.size())
		return items[slot];
	return 0;
}

void InventoryUI::draw(Graphics::Screen *screen) {
	if (!_open || !_loaded)
		return;
	blitImage(screen, _safeImage.pixels, 0, 0,
		_safeImage.rect.width(), _safeImage.rect.height());

	Common::Array<uint32> items = itemsInOrder();
	int perRow = 1;
	while ((kSlotWidth * (perRow + 1) + kSeparationX * perRow) <= (kAreaRight - kAreaLeft))
		perRow++;
	for (uint i = 0; i < items.size(); i++) {
		const ItemImages *images = itemImages(items[i]);
		if (!images)
			continue;
		int16 x = kAreaLeft + (int16)(i % perRow) * (kSlotWidth + kSeparationX);
		int16 y = kAreaTop + (int16)(i / perRow) * (kSlotHeight + kSeparationY);
		const byte *pixels = (items[i] == _hoverObject) ? images->highlight : images->normal;
		blitImage(screen, pixels, x, y, images->rect.width(), images->rect.height());
	}
}

} // End of namespace Gamebot
