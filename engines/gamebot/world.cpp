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
#include "common/system.h"
#include "graphics/paletteman.h"
#include "graphics/screen.h"

#include "gamebot/gamebot.h"
#include "gamebot/world.h"

namespace Gamebot {

// Colors 0 is transparent in every image
static const byte kTransparentColor = 0;

// Palette indexes used by the debug overlays; late entries chosen as
// they rarely clash with visible art
static const byte kOverlayWalkColor = 255;
static const byte kOverlayBlockColor = 254;
static const byte kOverlayHotspotColor = 253;
static const byte kOverlayExitColor = 252;

void World::clear() {
	for (uint i = 0; i < _items.size(); i++)
		delete[] _items[i].pixels;
	_items.clear();
	_hotspots.clear();
	_mapCells.clear();
	_mapBaseY.clear();
	_mapScale.clear();
	_mapWidth = _mapHeight = 0;
	_phaseId = 0;
	_phaseWidth = _phaseHeight = 0;
	_origin = Common::Point(0, 0);
}

bool World::loadPhaseInit(uint32 phaseId) {
	ResourceFile &res = g_engine->resources();
	const ResourceEntry *e = res.findResource(phaseId, kResPhaseInit);
	if (!e) {
		warning("Phase %08x has no phase init resource", phaseId);
		return false;
	}

	byte *data = res.readBlob(*e);
	if (!data)
		return false;

	// Size is stored as an inclusive bottom-right coordinate
	_phaseWidth = (int16)(READ_LE_UINT32(data) + 1);
	_phaseHeight = (int16)(READ_LE_UINT32(data + 4) + 1);

	// Palette entries are 4 bytes (red, green, blue, flags)
	byte palette[256 * 3];
	for (uint i = 0; i < 256; i++) {
		palette[i * 3] = data[16 + i * 4];
		palette[i * 3 + 1] = data[16 + i * 4 + 1];
		palette[i * 3 + 2] = data[16 + i * 4 + 2];
	}
	g_system->getPaletteManager()->setPalette(palette, 0, 256);

	debugC(kDebugResources, "Phase %08x: %dx%d, music %x, fx %x", phaseId,
		_phaseWidth, _phaseHeight, READ_LE_UINT32(data + 8), READ_LE_UINT32(data + 12));
	delete[] data;
	return true;
}

void World::loadWalkMap(uint32 phaseId) {
	// Walk maps belong to the characters, one per phase with the phase
	// id in the low word of the resource id
	ResourceFile &res = g_engine->resources();
	const ResourceEntry *e = nullptr;
	for (uint i = 0; i < res.count(); i++) {
		if (res.entry(i).type == kResPhaseMap && (res.entry(i).resId & 0xffff) == phaseId) {
			e = &res.entry(i);
			break;
		}
	}
	if (!e)
		return;

	byte *data = res.readBlob(*e);
	if (!data)
		return;

	_mapWidth = READ_LE_UINT32(data);
	_mapHeight = READ_LE_UINT32(data + 4);
	const byte *baseY = data + 8;
	const byte *scale = baseY + _mapHeight * 2;
	const byte *cells = scale + _mapHeight * 2;

	_mapBaseY.resize(_mapHeight);
	_mapScale.resize(_mapHeight);
	for (uint32 y = 0; y < _mapHeight; y++) {
		_mapBaseY[y] = READ_LE_UINT16(baseY + y * 2);
		_mapScale[y] = READ_LE_UINT16(scale + y * 2);
	}
	_mapCells.resize(_mapWidth * _mapHeight);
	memcpy(_mapCells.data(), cells, _mapWidth * _mapHeight);
	delete[] data;
}

// Picks the object's statically visible resource, if any: a plain
// image or the first frame of an automatic animation. Event-triggered
// animations stay dormant until the action system starts them.
void World::addDrawItem(const ObjectEntry &object) {
	ResourceFile &res = g_engine->resources();
	int i = res.findObject(object.objectId);

	for (; i >= 0 && i < (int)res.count() && res.entry(i).objectId == object.objectId; i++) {
		const ResourceEntry &e = res.entry(i);

		// Collect interaction areas for the hotspot overlay while at it
		if (e.type == kResHiddenImage || e.type == kResPhaseExit || e.type == kResMapExit) {
			byte *data = res.readBlob(e);
			if (data) {
				Hotspot hotspot;
				hotspot.objectId = object.objectId;
				hotspot.type = e.type;
				hotspot.rect = Common::Rect(
					READ_LE_INT32(data), READ_LE_INT32(data + 4),
					READ_LE_INT32(data + 8) + 1, READ_LE_INT32(data + 12) + 1);
				_hotspots.push_back(hotspot);
				delete[] data;
			}
			continue;
		}

		bool isImage = e.type == kResImage;
		bool isAutoAnimation = e.type == kResAnimationAuto || e.type == kResAnimationAutoMobile;
		if (!isImage && !isAutoAnimation)
			continue;

		byte *data = res.readBlob(e);
		if (!data)
			continue;

		DrawItem item;
		item.objectId = object.objectId;
		item.resId = e.resId;
		item.rect = Common::Rect(
			READ_LE_INT32(data), READ_LE_INT32(data + 4),
			READ_LE_INT32(data + 8) + 1, READ_LE_INT32(data + 12) + 1);
		const uint32 pixelCount = item.rect.width() * item.rect.height();
		item.pixels = new byte[pixelCount];

		if (isImage) {
			memcpy(item.pixels, data + 16, pixelCount);
		} else {
			// First frame shown by the sequence (1-based frame index)
			uint32 sequenceStart = READ_LE_UINT32(data + 16 + 20);
			uint32 imageLocation = READ_LE_UINT32(data + 16 + 16);
			uint32 frame = (sequenceStart >= 1 && sequenceStart < SequenceStep::kAutoDisable)
				? sequenceStart - 1 : 0;
			Common::File &file = res.file();
			file.seek(imageLocation + frame * pixelCount);
			if (file.read(item.pixels, pixelCount) != pixelCount) {
				warning("Could not read frame of animation %08x/%08x", e.objectId, e.resId);
				delete[] item.pixels;
				delete[] data;
				continue;
			}
		}
		_items.push_back(item);
		delete[] data;
		return; // one visible resource per object is enough for now
	}
}

bool World::gotoPhase(uint32 phaseId) {
	WorldFile &world = g_engine->initialWorld();
	int phaseIndex = world.findPhase(phaseId);
	if (phaseIndex < 0) {
		warning("Unknown phase %08x", phaseId);
		return false;
	}

	clear();
	if (!loadPhaseInit(phaseId))
		return false;
	_phaseId = phaseId;
	loadWalkMap(phaseId);

	// Draw order: last layer first (background), layer 1 last (front);
	// layer 0 holds the disabled objects and is skipped
	const PhaseEntry &phase = world.phase(phaseIndex);
	for (int l = (int)phase.layerCount - 1; l >= 1; l--) {
		const LayerEntry &layer = world.layer(phase.layerFirst + l);
		for (uint o = 0; o < layer.objectCount; o++)
			addDrawItem(world.object(layer.objectFirst + o));
	}

	debugC(kDebugResources, "Phase %08x loaded: %u draw items, %u hotspots, map %ux%u",
		phaseId, _items.size(), _hotspots.size(), _mapWidth, _mapHeight);
	return true;
}

// Blits an 8bpp image at absolute phase coordinates onto the screen,
// honoring the scroll origin, clipping and the transparent color
static void blitItem(Graphics::Screen *screen, const byte *pixels,
		const Common::Rect &rect, const Common::Point &origin) {
	Common::Rect dest(rect);
	dest.translate(-origin.x, -origin.y);
	Common::Rect clipped(dest);
	clipped.clip(Common::Rect(0, 0, screen->w, screen->h));
	if (clipped.isEmpty())
		return;

	const int srcPitch = rect.width();
	for (int y = clipped.top; y < clipped.bottom; y++) {
		const byte *src = pixels + (y - dest.top) * srcPitch + (clipped.left - dest.left);
		byte *dst = (byte *)screen->getBasePtr(clipped.left, y);
		for (int x = clipped.width(); x > 0; x--, src++, dst++) {
			if (*src != kTransparentColor)
				*dst = *src;
		}
	}
}

void World::draw(Graphics::Screen *screen) {
	screen->clear(kTransparentColor);
	for (uint i = 0; i < _items.size(); i++)
		blitItem(screen, _items[i].pixels, _items[i].rect, _origin);

	if (_showWalkMap)
		drawWalkMapOverlay(screen);
	if (_showHotspots)
		drawHotspotOverlay(screen);
	screen->markAllDirty();
}

void World::drawWalkMapOverlay(Graphics::Screen *screen) const {
	if (!_mapWidth)
		return;

	// Rows are bands ending at baseY; columns are kWalkCellWidth wide.
	// Walkable cells get a dotted fill, blocked ones stay clear.
	uint16 bandTop = 0;
	for (uint32 y = 0; y < _mapHeight; y++) {
		uint16 bandBottom = _mapBaseY[y];
		for (uint32 x = 0; x < _mapWidth; x++) {
			byte cell = _mapCells[y * _mapWidth + x];
			bool walkable = (cell & kCellWalkable) != 0;
			for (int py = bandTop; py <= bandBottom; py += 3) {
				for (int px = x * kWalkCellWidth; px < (int)(x + 1) * kWalkCellWidth; px += 3) {
					int sx = px - _origin.x, sy = py - _origin.y;
					if (sx >= 0 && sx < screen->w && sy >= 0 && sy < screen->h && walkable)
						*(byte *)screen->getBasePtr(sx, sy) = kOverlayWalkColor;
				}
			}
		}
		// Band separator line
		int sy = bandBottom - _origin.y;
		if (sy >= 0 && sy < screen->h) {
			for (int sx = 0; sx < screen->w; sx++)
				*(byte *)screen->getBasePtr(sx, sy) = kOverlayBlockColor;
		}
		bandTop = bandBottom + 1;
	}
}

void World::drawHotspotOverlay(Graphics::Screen *screen) const {
	for (uint i = 0; i < _hotspots.size(); i++) {
		Common::Rect r(_hotspots[i].rect);
		r.translate(-_origin.x, -_origin.y);
		r.clip(Common::Rect(0, 0, screen->w, screen->h));
		if (r.isEmpty())
			continue;
		byte color = (_hotspots[i].type == kResHiddenImage) ? kOverlayHotspotColor : kOverlayExitColor;
		screen->frameRect(r, color);
	}
}

} // End of namespace Gamebot
