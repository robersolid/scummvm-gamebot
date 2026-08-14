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

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/endian.h"
#include "common/system.h"
#include "engines/metaengine.h"
#include "graphics/font.h"
#include "graphics/paletteman.h"
#include "graphics/screen.h"

#include "gamebot/character.h"
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

bool VerbPalette::contains(const Common::Point &screenPos) const {
	if (!_open)
		return false;
	Common::Rect area(_pos.x, _pos.y, _pos.x + _background.rect.width(),
		_pos.y + _background.rect.height());
	return area.contains(screenPos);
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

// Menu resources of the original OptionsMaster: the panel background
// (object c000), one object per button with normal/hover/pressed
// images, and the menu palette in the phase init of object 0x13
static const uint32 kMenuBackgroundId = 0xc000;
static const uint32 kMenuFirstButtonId = 0xc001; // Nueva..Salir are consecutive

MainMenu::~MainMenu() {
	delete[] _background.pixels;
	for (uint i = 0; i < kActionCount; i++) {
		delete[] _buttons[i].pixels;
		delete[] _highlights[i].pixels;
	}
}

bool MainMenu::loadImage(uint32 objectId, uint imageIndex, Image &image) {
	ResourceFile &res = g_engine->resources();
	int i = res.findObject(objectId);
	uint seen = 0;
	for (; i >= 0 && i < (int)res.count() && res.entry(i).objectId == objectId; i++) {
		if (res.entry(i).type != kResImage || seen++ != imageIndex)
			continue;
		byte *data = res.readBlob(res.entry(i));
		if (!data)
			return false;
		image.rect = Common::Rect(
			READ_LE_INT32(data), READ_LE_INT32(data + 4),
			READ_LE_INT32(data + 8) + 1, READ_LE_INT32(data + 12) + 1);
		image.pixels = new byte[image.rect.width() * image.rect.height()];
		memcpy(image.pixels, data + 16, image.rect.width() * image.rect.height());
		delete[] data;
		return true;
	}
	return false;
}

bool MainMenu::load() {
	if (!loadImage(kMenuBackgroundId, 0, _background))
		return false;
	for (uint i = 0; i < kActionCount; i++) {
		loadImage(kMenuFirstButtonId + i, 0, _buttons[i]);
		loadImage(kMenuFirstButtonId + i, 1, _highlights[i]);
	}

	// The menu palette lives in the OptionsMaster phase init
	const ResourceEntry *e = g_engine->resources().findResource(0x13, kResPhaseInit);
	byte *data = e ? g_engine->resources().readBlob(*e) : nullptr;
	if (data) {
		for (uint i = 0; i < 256; i++) {
			_palette[i * 3] = data[16 + i * 4];
			_palette[i * 3 + 1] = data[16 + i * 4 + 1];
			_palette[i * 3 + 2] = data[16 + i * 4 + 2];
		}
		delete[] data;
	}
	_loaded = true;
	return true;
}

void MainMenu::open() {
	if (!_loaded && !load())
		return;
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	_hover = -1;
	_open = true;
}

void MainMenu::close() {
	_open = false;
	g_engine->world().applyPalette();
}

int MainMenu::hitButton(const Common::Point &screenPos) const {
	for (uint i = 0; i < kActionCount; i++) {
		const Image &button = _buttons[i];
		if (!button.pixels || !button.rect.contains(screenPos))
			continue;
		byte pixel = button.pixels[
			(screenPos.y - button.rect.top) * button.rect.width() +
			(screenPos.x - button.rect.left)];
		if (pixel != kTransparentColor)
			return (int)i;
	}
	return -1;
}

void MainMenu::updateHover(const Common::Point &screenPos) {
	if (_open)
		_hover = hitButton(screenPos);
}

MainMenu::Action MainMenu::handleClick(const Common::Point &screenPos) {
	int button = hitButton(screenPos);
	return (button >= 0) ? (Action)button : kActionNone;
}

void MainMenu::draw(Graphics::Screen *screen) const {
	if (!_open || !_loaded)
		return;
	blitImage(screen, _background.pixels, _background.rect.left, _background.rect.top,
		_background.rect.width(), _background.rect.height());
	for (uint i = 0; i < kActionCount; i++) {
		const Image &image = ((int)i == _hover && _highlights[i].pixels)
			? _highlights[i] : _buttons[i];
		if (image.pixels)
			blitImage(screen, image.pixels, image.rect.left, image.rect.top,
				image.rect.width(), image.rect.height());
	}
}

// Medallion resources of the ChangerMaster object (id 0x15): four
// face images (Mortadelo, Filemon and their grayed variants) plus a
// spin animation per direction
static const uint32 kChangerObjectId = 0x15;
static const uint32 kFaceMortadelo = 0x00150101;
static const uint32 kFaceFilemon = 0x00150102;
static const uint32 kSpinToFilemon = 0x00150501;
static const uint32 kSpinToMortadelo = 0x00150502;

ChangerBadge::~ChangerBadge() {
	for (uint i = 0; i < 2; i++) {
		delete[] _faces[i].pixels;
		delete[] _spins[i].frames;
	}
}

bool ChangerBadge::load() {
	ResourceFile &res = g_engine->resources();
	static const uint32 kFaceIds[2] = { kFaceMortadelo, kFaceFilemon };
	static const uint32 kSpinIds[2] = { kSpinToMortadelo, kSpinToFilemon };

	for (uint i = 0; i < 2; i++) {
		const ResourceEntry *e = res.findByResId(kFaceIds[i]);
		byte *data = e ? res.readBlob(*e) : nullptr;
		if (!data)
			return false;
		_faces[i].pixels = new byte[kBadgeWidth * kBadgeHeight];
		memcpy(_faces[i].pixels, data + 16, kBadgeWidth * kBadgeHeight);
		delete[] data;

		e = res.findByResId(kSpinIds[i]);
		data = e ? res.readBlob(*e) : nullptr;
		if (!data)
			continue; // faces alone still work
		Spin &spin = _spins[i];
		spin.frameSize = kBadgeWidth * kBadgeHeight;
		spin.imageCount = READ_LE_UINT32(data + 16);
		uint32 sequenceCount = READ_LE_UINT32(data + 20);
		spin.framePeriod = READ_LE_UINT32(data + 24) * 3; // original tick pacing
		uint32 imageLocation = READ_LE_UINT32(data + 32);
		spin.sequence.resize(sequenceCount);
		for (uint32 s = 0; s < sequenceCount; s++)
			spin.sequence[s] = READ_LE_UINT32(data + 36 + s * 12);
		delete[] data;

		spin.frames = new byte[spin.frameSize * spin.imageCount];
		Common::File &file = res.file();
		file.seek(imageLocation);
		if (file.read(spin.frames, spin.frameSize * spin.imageCount) !=
				spin.frameSize * spin.imageCount) {
			delete[] spin.frames;
			spin.frames = nullptr;
		}
	}
	_loaded = true;
	return true;
}

int16 ChangerBadge::badgeX() const {
	return kScreenWidth - kBadgeWidth;
}

bool ChangerBadge::handleClick(const Common::Point &screenPos) {
	if (!_loaded || _spinning || !g_engine->secondCharacter())
		return false;
	int localX = screenPos.x - badgeX(), localY = screenPos.y;
	if (localX < 0 || localX >= kBadgeWidth || localY < 0 || localY >= kBadgeHeight)
		return false;

	uint facing = (g_engine->master().objectId() == kCharMortadelo) ? 1 : 0;
	if (_faces[facing].pixels[localY * kBadgeWidth + localX] == kTransparentColor)
		return false;

	// Spin toward the other character, then hand over control
	_spinIndex = facing;
	if (_spins[_spinIndex].frames) {
		_spinning = true;
		_spinStep = 0;
		_spinTime = 0;
	} else {
		g_engine->switchMaster();
	}
	return true;
}

void ChangerBadge::update(uint32 millis) {
	if (!_spinning)
		return;
	Spin &spin = _spins[_spinIndex];
	if (!_spinTime)
		_spinTime = millis + spin.framePeriod;
	while (millis >= _spinTime && _spinning) {
		_spinTime += spin.framePeriod ? spin.framePeriod : 15;
		_spinStep++;
		if (_spinStep >= spin.sequence.size() ||
				spin.sequence[_spinStep] >= SequenceStep::kAutoDisable) {
			_spinning = false;
			g_engine->switchMaster();
		}
	}
}

void ChangerBadge::draw(Graphics::Screen *screen) {
	if (!_loaded || !g_engine->secondCharacter())
		return;

	const byte *pixels;
	if (_spinning) {
		Spin &spin = _spins[_spinIndex];
		uint32 imageIndex = spin.sequence.empty() ? 1 : spin.sequence[_spinStep];
		if (imageIndex < 1 || imageIndex > spin.imageCount)
			imageIndex = 1;
		pixels = spin.frames + (imageIndex - 1) * spin.frameSize;
	} else {
		// The badge shows the partner you would switch to
		uint facing = (g_engine->master().objectId() == kCharMortadelo) ? 1 : 0;
		pixels = _faces[facing].pixels;
	}
	blitImage(screen, pixels, badgeX(), 0, kBadgeWidth, kBadgeHeight);
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

const byte *InventoryUI::itemCursor(uint32 objectId, int16 &width, int16 &height, bool highlighted) {
	const ItemImages *images = itemImages(objectId);
	if (!images)
		return nullptr;
	width = images->rect.width();
	height = images->rect.height();
	// The highlighted variant carries the red outline the original
	// shows while the carried object hovers something interactable
	if (highlighted && images->highlight)
		return images->highlight;
	return images->normal;
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

// Object codes of the panels (original MainClass.h OptObj* defines)
enum {
	kOptLoadPanel = 0xc100,
	kOptLoadButton = 0xc101,
	kOptLoadBack = 0xc102,
	kOptLoadEntry = 0xc103,
	kOptSavePanel = 0xc200,
	kOptSaveButton = 0xc201,
	kOptSaveBack = 0xc202,
	kOptSaveEntry = 0xc203,
	kOptOptionsPanel = 0xc300,
	kOptBarFirst = 0xc301,       // effects, music, voices
	kOptBarButtonFirst = 0xc304, // more/less pairs per bar
	kOptOptionsBack = 0xc30C,
	kOptClickSound = 0x151001
};

// ScummVM volume keys matched to the three bars
static const char *kVolumeKeys[3] = { "sfx_volume", "music_volume", "speech_volume" };

OptionsPanels::~OptionsPanels() {
	delete[] _loadBg.pixels;
	delete[] _saveBg.pixels;
	delete[] _optionsBg.pixels;
	for (uint i = 0; i < kSlotCount; i++) {
		delete[] _loadSlots[i].pixels;
		delete[] _saveSlots[i].pixels;
	}
	for (uint b = 0; b < 3; b++) {
		for (uint l = 0; l < kBarLevels; l++)
			delete[] _bars[b][l].pixels;
		for (uint st = 0; st < 3; st++) {
			delete[] _barMore[b].states[st].pixels;
			delete[] _barLess[b].states[st].pixels;
		}
	}
	for (uint st = 0; st < 3; st++) {
		delete[] _loadAction.states[st].pixels;
		delete[] _loadBack.states[st].pixels;
		delete[] _saveAction.states[st].pixels;
		delete[] _saveBack.states[st].pixels;
		delete[] _optionsBack.states[st].pixels;
	}
}

bool OptionsPanels::loadImage(uint32 objectId, uint imageIndex, Image &image) const {
	ResourceFile &res = g_engine->resources();
	int i = res.findObject(objectId);
	uint seen = 0;
	for (; i >= 0 && i < (int)res.count() && res.entry(i).objectId == objectId; i++) {
		if (res.entry(i).type != kResImage || seen++ != imageIndex)
			continue;
		byte *data = res.readBlob(res.entry(i));
		if (!data)
			return false;
		image.rect = Common::Rect(
			READ_LE_INT32(data), READ_LE_INT32(data + 4),
			READ_LE_INT32(data + 8) + 1, READ_LE_INT32(data + 12) + 1);
		image.pixels = new byte[image.rect.width() * image.rect.height()];
		memcpy(image.pixels, data + 16, image.rect.width() * image.rect.height());
		delete[] data;
		return true;
	}
	return false;
}

void OptionsPanels::loadButton(uint32 objectId, Button &button) const {
	for (uint st = 0; st < 3; st++)
		loadImage(objectId, st, button.states[st]);
}

bool OptionsPanels::load() {
	if (!loadImage(kOptLoadPanel, 0, _loadBg) ||
			!loadImage(kOptSavePanel, 0, _saveBg) ||
			!loadImage(kOptOptionsPanel, 0, _optionsBg))
		return false;
	loadButton(kOptLoadButton, _loadAction);
	loadButton(kOptLoadBack, _loadBack);
	loadButton(kOptSaveButton, _saveAction);
	loadButton(kOptSaveBack, _saveBack);
	loadButton(kOptOptionsBack, _optionsBack);
	for (uint i = 0; i < kSlotCount; i++) {
		loadImage(kOptLoadEntry + i, 0, _loadSlots[i]);
		loadImage(kOptSaveEntry + i, 0, _saveSlots[i]);
	}
	for (uint b = 0; b < 3; b++) {
		for (uint l = 0; l < kBarLevels; l++)
			loadImage(kOptBarFirst + b, l, _bars[b][l]);
		loadButton(kOptBarButtonFirst + b * 2, _barMore[b]);
		loadButton(kOptBarButtonFirst + b * 2 + 1, _barLess[b]);
	}
	_loaded = true;
	return true;
}

// The save dates come from the existing ScummVM saves of slots 1-10
void OptionsPanels::refreshSaves() {
	for (uint i = 0; i < kSlotCount; i++) {
		_slotText[i].clear();
		_slotUsed[i] = false;
	}
	SaveStateList saves = g_engine->getMetaEngine()->listSaves(
		g_engine->targetName().c_str());
	for (uint i = 0; i < saves.size(); i++) {
		int slot = saves[i].getSaveSlot() - 1;
		if (slot < 0 || slot >= (int)kSlotCount)
			continue;
		_slotUsed[slot] = true;
		_slotText[slot] = saves[i].getDescription();
	}
}

void OptionsPanels::open(Panel panel) {
	if (!_loaded && !load())
		return;
	if (panel == kPanelLoad || panel == kPanelSave)
		refreshSaves();
	_marked = -1;
	_hover = -1;
	_panel = panel;
}

int OptionsPanels::hitImage(const Common::Point &screenPos, const Image &image) const {
	if (!image.pixels || !image.rect.contains(screenPos))
		return -1;
	byte pixel = image.pixels[
		(screenPos.y - image.rect.top) * image.rect.width() +
		(screenPos.x - image.rect.left)];
	return (pixel != kTransparentColor) ? 1 : -1;
}

int OptionsPanels::volumeLevel(uint bar) const {
	int volume = ConfMan.getInt(kVolumeKeys[bar]);
	return CLIP(volume * (int)kBarLevels / 256, 0, (int)kBarLevels - 1);
}

void OptionsPanels::setVolumeLevel(uint bar, int level) {
	level = CLIP(level, 0, (int)kBarLevels - 1);
	ConfMan.setInt(kVolumeKeys[bar], level * 255 / ((int)kBarLevels - 1));
	g_engine->syncSoundSettings();
}

void OptionsPanels::updateHover(const Common::Point &screenPos) {
	_hover = -1;
	if (_panel == kPanelLoad) {
		if (hitImage(screenPos, _loadAction.states[0]) > 0)
			_hover = 0;
		else if (hitImage(screenPos, _loadBack.states[0]) > 0)
			_hover = 1;
	} else if (_panel == kPanelSave) {
		if (hitImage(screenPos, _saveAction.states[0]) > 0)
			_hover = 0;
		else if (hitImage(screenPos, _saveBack.states[0]) > 0)
			_hover = 1;
	} else if (_panel == kPanelOptions) {
		if (hitImage(screenPos, _optionsBack.states[0]) > 0)
			_hover = 1;
		for (uint b = 0; b < 3; b++) {
			if (hitImage(screenPos, _barMore[b].states[0]) > 0)
				_hover = 10 + (int)b * 2;
			else if (hitImage(screenPos, _barLess[b].states[0]) > 0)
				_hover = 10 + (int)b * 2 + 1;
		}
	}
}

bool OptionsPanels::handleClick(const Common::Point &screenPos) {
	SoundManager &sounds = g_engine->sounds();
	if (_panel == kPanelLoad || _panel == kPanelSave) {
		const Image *slots = (_panel == kPanelLoad) ? _loadSlots : _saveSlots;
		for (uint i = 0; i < kSlotCount; i++) {
			if (slots[i].rect.contains(screenPos)) {
				_marked = (int)i;
				return false;
			}
		}
		const Button &action = (_panel == kPanelLoad) ? _loadAction : _saveAction;
		const Button &back = (_panel == kPanelLoad) ? _loadBack : _saveBack;
		if (hitImage(screenPos, action.states[0]) > 0) {
			sounds.playSound(kOptClickSound, Audio::Mixer::kSFXSoundType);
			if (_marked < 0)
				return false;
			if (_panel == kPanelLoad) {
				if (!_slotUsed[_marked])
					return false;
				if (g_engine->loadGameState(_marked + 1).getCode() == Common::kNoError) {
					close();
					return true; // the whole menu closes
				}
				return false;
			}
			// Saving stamps the current date as the slot text
			TimeDate td;
			g_system->getTimeAndDate(td);
			Common::String desc = Common::String::format(
				"%02d/%02d/%04d %02d:%02d", td.tm_mday, td.tm_mon + 1,
				td.tm_year + 1900, td.tm_hour, td.tm_min);
			g_engine->saveGameState(_marked + 1, desc);
			refreshSaves();
			return false;
		}
		if (hitImage(screenPos, back.states[0]) > 0) {
			sounds.playSound(kOptClickSound, Audio::Mixer::kSFXSoundType);
			close();
		}
		return false;
	}

	if (_panel == kPanelOptions) {
		for (uint b = 0; b < 3; b++) {
			if (hitImage(screenPos, _barMore[b].states[0]) > 0) {
				sounds.playSound(kOptClickSound, Audio::Mixer::kSFXSoundType);
				setVolumeLevel(b, volumeLevel(b) + 1);
				return false;
			}
			if (hitImage(screenPos, _barLess[b].states[0]) > 0) {
				sounds.playSound(kOptClickSound, Audio::Mixer::kSFXSoundType);
				setVolumeLevel(b, volumeLevel(b) - 1);
				return false;
			}
		}
		if (hitImage(screenPos, _optionsBack.states[0]) > 0) {
			sounds.playSound(kOptClickSound, Audio::Mixer::kSFXSoundType);
			close();
		}
	}
	return false;
}

void OptionsPanels::drawButton(Graphics::Screen *screen, const Button &button,
		bool hot) const {
	const Image &image = (hot && button.states[1].pixels)
		? button.states[1] : button.states[0];
	if (image.pixels)
		blitImage(screen, image.pixels, image.rect.left, image.rect.top,
			image.rect.width(), image.rect.height());
}

void OptionsPanels::draw(Graphics::Screen *screen) {
	if (_panel == kPanelNone)
		return;

	if (_panel == kPanelOptions) {
		blitImage(screen, _optionsBg.pixels, 0, 0,
			_optionsBg.rect.width(), _optionsBg.rect.height());
		for (uint b = 0; b < 3; b++) {
			const Image &bar = _bars[b][volumeLevel(b)];
			if (bar.pixels)
				blitImage(screen, bar.pixels, bar.rect.left, bar.rect.top,
					bar.rect.width(), bar.rect.height());
			drawButton(screen, _barMore[b], _hover == 10 + (int)b * 2);
			drawButton(screen, _barLess[b], _hover == 10 + (int)b * 2 + 1);
		}
		drawButton(screen, _optionsBack, _hover == 1);
		return;
	}

	const Image &bg = (_panel == kPanelLoad) ? _loadBg : _saveBg;
	const Image *slots = (_panel == kPanelLoad) ? _loadSlots : _saveSlots;
	blitImage(screen, bg.pixels, 0, 0, bg.rect.width(), bg.rect.height());

	const Graphics::Font *font = TextWriter::dialogFont();
	for (uint i = 0; i < kSlotCount; i++) {
		const Image &line = slots[i];
		if ((int)i == _marked && line.pixels)
			blitImage(screen, line.pixels, line.rect.left, line.rect.top,
				line.rect.width(), line.rect.height());
		if (font && !_slotText[i].empty()) {
			// The original prints the save date centered in the line,
			// in the yellow options color
			byte color = 255;
			byte palette[256 * 3];
			g_system->getPaletteManager()->grabPalette(palette, 0, 256);
			uint32 best = 0xffffffff;
			for (uint c = 0; c < 256; c++) {
				int dr = (int)palette[c * 3] - 255;
				int dg = (int)palette[c * 3 + 1] - 255;
				int db = (int)palette[c * 3 + 2] - 5;
				uint32 dist = (uint32)(dr * dr + dg * dg + db * db);
				if (dist < best) {
					best = dist;
					color = (byte)c;
				}
			}
			Common::U32String text(_slotText[i], Common::kISO8859_1);
			font->drawString(screen, text, line.rect.left, line.rect.top + 1,
				line.rect.width(), color, Graphics::kTextAlignCenter);
		}
	}

	drawButton(screen, (_panel == kPanelLoad) ? _loadAction : _saveAction, _hover == 0);
	drawButton(screen, (_panel == kPanelLoad) ? _loadBack : _saveBack, _hover == 1);
}

} // End of namespace Gamebot
