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

#include "gamebot/character.h"
#include "gamebot/debug-names.h"
#include "gamebot/gamebot.h"
#include "gamebot/world.h"

namespace Gamebot {

// Color 0 is transparent in every image
static const byte kTransparentColor = 0;

// Palette indexes used by the debug overlays; late entries chosen as
// they rarely clash with visible art
static const byte kOverlayWalkColor = 255;
static const byte kOverlayBlockColor = 254;
static const byte kOverlayHotspotColor = 253;
static const byte kOverlayExitColor = 252;

void World::clear() {
	for (uint i = 0; i < _items.size(); i++) {
		delete[] _items[i].staticPixels;
		for (uint a = 0; a < _items[i].anims.size(); a++)
			delete[] _items[i].anims[a].frames;
	}
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

// Collects the object's visible resources: an optional static image
// plus every automatic animation. In the original engine each auto
// animation registers its own timer; when it fires it becomes the
// object's active resource and plays. Event-triggered animations
// stay dormant until the action system starts them.
void World::addDrawItem(const ObjectEntry &object, uint16 layer) {
	// Generic system objects (writer, inventory, fx, changer...) have
	// custom draw paths in the original engine: the inventory image,
	// for instance, is only drawn while the inventory is open. They
	// are skipped until those subsystems exist.
	if (object.objectId < 0x100)
		return;

	ResourceFile &res = g_engine->resources();
	const int first = res.findObject(object.objectId);

	DrawItem item;
	item.objectId = object.objectId;
	item.layer = layer;
	item.name = object.name;

	for (int i = first; i >= 0 && i < (int)res.count() &&
			res.entry(i).objectId == object.objectId; i++) {
		const ResourceEntry &e = res.entry(i);

		// Collect interaction areas for hit tests and the overlay
		if (e.type == kResHiddenImage || e.type == kResPhaseExit || e.type == kResMapExit) {
			byte *data = res.readBlob(e);
			if (data) {
				Hotspot hotspot;
				hotspot.objectId = object.objectId;
				hotspot.name = object.name;
				hotspot.type = e.type;
				hotspot.rect = Common::Rect(
					READ_LE_INT32(data), READ_LE_INT32(data + 4),
					READ_LE_INT32(data + 8) + 1, READ_LE_INT32(data + 12) + 1);
				if (e.type != kResHiddenImage && e.size >= 16 + 4)
					hotspot.exitPhase = READ_LE_UINT32(data + 16);
				_hotspots.push_back(hotspot);
				delete[] data;
			}
			continue;
		}

		if (e.type == kResImage && !item.staticPixels) {
			byte *data = res.readBlob(e);
			if (!data)
				continue;
			item.rect = Common::Rect(
				READ_LE_INT32(data), READ_LE_INT32(data + 4),
				READ_LE_INT32(data + 8) + 1, READ_LE_INT32(data + 12) + 1);
			item.staticPixels = new byte[item.rect.width() * item.rect.height()];
			memcpy(item.staticPixels, data + 16, item.rect.width() * item.rect.height());
			delete[] data;
			continue;
		}

		bool isAuto = e.type == kResAnimationAuto || e.type == kResAnimationAutoMobile;
		bool isEvent = e.type == kResAnimationEvent || e.type == kResAnimationEventMobile;
		if (isAuto || isEvent) {
			Animation anim;
			if (loadAnimation(e, anim)) {
				anim.autoFire = isAuto;
				item.anims.push_back(anim);
			}
		}
	}

	if (!item.staticPixels && item.anims.empty())
		return;

	// Without a static image the first animation provides the idle look
	if (!item.staticPixels)
		item.activeAnim = 0;

	debugC(2, kDebugResources, "Item %08x '%s': static=%d anims=%u",
		item.objectId, item.name.c_str(), item.staticPixels != nullptr, item.anims.size());
	_items.push_back(item);
}

bool World::loadAnimation(const ResourceEntry &e, Animation &anim) {
	ResourceFile &res = g_engine->resources();
	byte *data = res.readBlob(e);
	if (!data)
		return false;

	anim.resId = e.resId;
	anim.rect = Common::Rect(
		READ_LE_INT32(data), READ_LE_INT32(data + 4),
		READ_LE_INT32(data + 8) + 1, READ_LE_INT32(data + 12) + 1);
	anim.frameSize = anim.rect.width() * anim.rect.height();
	anim.params.imageCount = READ_LE_UINT32(data + 16);
	anim.params.sequenceCount = READ_LE_UINT32(data + 20);
	anim.params.framePeriod = READ_LE_UINT32(data + 24);
	anim.params.startPause = READ_LE_UINT32(data + 28);
	anim.params.imageLocation = READ_LE_UINT32(data + 32);

	if (!anim.params.imageCount || !anim.params.sequenceCount) {
		delete[] data;
		return false;
	}

	anim.sequence.resize(anim.params.sequenceCount);
	for (uint32 s = 0; s < anim.params.sequenceCount; s++) {
		const byte *p = data + 36 + s * 12;
		anim.sequence[s].imageIndex = READ_LE_UINT32(p);
		anim.sequence[s].soundCode = READ_LE_UINT32(p + 4);
		anim.sequence[s].textCode = READ_LE_UINT32(p + 8);
	}

	// Mobile animations append a per-step displacement after the
	// inline frame data
	if (e.type == kResAnimationAutoMobile || e.type == kResAnimationEventMobile) {
		uint32 deltaOffset = 36 + anim.params.sequenceCount * 12 +
			anim.params.imageCount * anim.frameSize;
		if (deltaOffset + 4 <= e.size) {
			anim.stepDeltaX = READ_LE_INT16(data + deltaOffset);
			anim.stepDeltaY = READ_LE_INT16(data + deltaOffset + 2);
		}
	}
	delete[] data;

	anim.frames = new byte[anim.frameSize * anim.params.imageCount];
	Common::File &file = res.file();
	file.seek(anim.params.imageLocation);
	if (file.read(anim.frames, anim.frameSize * anim.params.imageCount) !=
			anim.frameSize * anim.params.imageCount) {
		warning("Could not read frames of animation %08x/%08x", e.objectId, e.resId);
		delete[] anim.frames;
		anim.frames = nullptr;
		return false;
	}
	return true;
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
			addDrawItem(world.object(layer.objectFirst + o), (uint16)l);
	}

	debugC(kDebugResources, "Phase %08x loaded: %u draw items, %u hotspots, map %ux%u",
		phaseId, _items.size(), _hotspots.size(), _mapWidth, _mapHeight);
	return true;
}

void World::updateItem(DrawItem &item, uint32 millis) {
	// While an event animation runs (started by rule or dialog), the
	// automatic ones wait, as in the original tick handling
	bool eventAnimRunning = item.activeAnim >= 0 &&
		item.anims[item.activeAnim].running && !item.anims[item.activeAnim].autoFire;

	// Start whichever animation reaches its fire time; a firing
	// animation becomes the active resource of the object
	for (uint a = 0; a < item.anims.size() && !eventAnimRunning; a++) {
		Animation &anim = item.anims[a];
		if (anim.running || !anim.autoFire)
			continue;
		if (!anim.fireTime) {
			anim.fireTime = millis + anim.params.startPause + anim.params.framePeriod;
			continue;
		}
		if (millis >= anim.fireTime) {
			anim.running = true;
			anim.seqPos = 0;
			anim.stepTime = millis + anim.params.framePeriod;
			item.activeAnim = (int)a;
			item.curDeltaX = item.curDeltaY = 0;
			debugC(2, kDebugEvents, "Animation %08x/%08x starts",
				item.objectId, anim.resId);
		}
	}

	if (item.activeAnim < 0)
		return;
	Animation &anim = item.anims[item.activeAnim];
	if (!anim.running)
		return;

	while (millis >= anim.stepTime && anim.running) {
		anim.stepTime += anim.params.framePeriod ? anim.params.framePeriod : 100;
		anim.seqPos++;

		uint32 imageIndex = (anim.seqPos < anim.sequence.size())
			? anim.sequence[anim.seqPos].imageIndex : SequenceStep::kAutoDisable;

		if (imageIndex == SequenceStep::kGotoBegin) {
			anim.seqPos = 0;
			imageIndex = anim.sequence[0].imageIndex;
		} else if (imageIndex == SequenceStep::kAutoDestroy ||
				imageIndex == SequenceStep::kAutoDisable) {
			// Sequence over: rearm the clock and show the idle look
			anim.running = false;
			anim.seqPos = 0;
			anim.fireTime = millis + anim.params.startPause + anim.params.framePeriod;
			if (item.staticPixels) {
				item.activeAnim = -1;
				item.curDeltaX = item.curDeltaY = 0;
			}
			debugC(2, kDebugEvents, "Animation %08x/%08x ends",
				item.objectId, anim.resId);
			if (anim.notifyEnd) {
				anim.notifyEnd = false;
				g_engine->logic().onAnimationEnded(anim.resId);
			}
			break;
		}

		item.curDeltaX = (int16)(anim.seqPos * anim.stepDeltaX);
		item.curDeltaY = (int16)(anim.seqPos * anim.stepDeltaY);
		debugC(3, kDebugEvents, "Animation %08x/%08x step %u frame %u",
			item.objectId, anim.resId, anim.seqPos, imageIndex);

		if (anim.seqPos < anim.sequence.size()) {
			const SequenceStep &step = anim.sequence[anim.seqPos];
			if (step.soundCode)
				debugC(2, kDebugSound, "Animation %08x/%08x wants sound %x",
					item.objectId, anim.resId, step.soundCode);
			// NPC speech: answer animations carry the spoken text of
			// each frame in the sequence
			if (step.textCode)
				g_engine->logic().writer().showTextCode(step.textCode);
		}
	}
}

void World::update(uint32 millis) {
	for (uint i = 0; i < _items.size(); i++) {
		if (!_items[i].anims.empty() && _items[i].visible)
			updateItem(_items[i], millis);
	}
}

bool World::isEnabled(uint32 objectId) const {
	for (uint i = 0; i < _items.size(); i++) {
		if (_items[i].objectId == objectId)
			return _items[i].visible;
	}
	return false;
}

void World::setEnabled(uint32 objectId, bool enabled) {
	// The original moves disabled objects to layer 0; visibility is
	// the observable effect for both drawing and hit tests
	for (uint i = 0; i < _items.size(); i++) {
		if (_items[i].objectId == objectId) {
			_items[i].visible = enabled;
			debugC(kDebugActions, "Object %08x %s", objectId, enabled ? "enabled" : "disabled");
			return;
		}
	}
	debugC(kDebugActions, "Object %08x not in this phase (%s ignored)",
		objectId, enabled ? "enable" : "disable");
}

bool World::startAnimation(uint32 objectId, uint32 resId) {
	for (uint i = 0; i < _items.size(); i++) {
		DrawItem &item = _items[i];
		if (item.objectId != objectId)
			continue;
		for (uint a = 0; a < item.anims.size(); a++) {
			if (item.anims[a].resId != resId)
				continue;
			Animation &anim = item.anims[a];
			anim.running = true;
			anim.seqPos = 0;
			anim.stepTime = g_system->getMillis() + anim.params.framePeriod;
			anim.notifyEnd = true;
			item.activeAnim = (int)a;
			item.visible = true;
			item.curDeltaX = item.curDeltaY = 0;
			debugC(kDebugActions, "Animation %08x/%08x started by rule", objectId, resId);
			return true;
		}
	}
	debugC(kDebugActions, "Animation %08x/%08x not available in this phase", objectId, resId);
	return false;
}

bool World::hitTest(const Common::Point &pos, HitResult &result) const {
	// Front-most first: items were stored back to front
	for (int i = (int)_items.size() - 1; i >= 0; i--) {
		const DrawItem &item = _items[i];
		Common::Rect r = item.currentRect();
		if (!item.visible || !r.contains(pos))
			continue;
		byte pixel = item.currentPixels()[(pos.y - r.top) * r.width() + (pos.x - r.left)];
		if (pixel == kTransparentColor)
			continue;
		// Nameless items (backgrounds and scenery) don't take hits
		if (item.name.empty())
			continue;
		result.objectId = item.objectId;
		result.name = item.name;
		result.type = (item.activeAnim >= 0) ? kResAnimationAuto : kResImage;
		result.exitPhase = 0;
		return true;
	}

	for (uint i = 0; i < _hotspots.size(); i++) {
		if (!_hotspots[i].rect.contains(pos))
			continue;
		result.objectId = _hotspots[i].objectId;
		result.name = _hotspots[i].name;
		result.type = _hotspots[i].type;
		result.exitPhase = _hotspots[i].exitPhase;
		return true;
	}
	return false;
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

void World::draw(Graphics::Screen *screen, const Character *actor) {
	screen->clear(kTransparentColor);

	// Items are ordered by descending layer. The actor draws after
	// all items of its own layer: the original engine inserts the
	// character at the end of its layer's object list.
	bool actorDrawn = (actor == nullptr) || !actor->isLoaded();
	for (uint i = 0; i < _items.size(); i++) {
		if (!actorDrawn && _items[i].layer < actor->layer()) {
			actor->draw(screen, _origin);
			actorDrawn = true;
		}
		if (_items[i].visible)
			blitItem(screen, _items[i].currentPixels(), _items[i].currentRect(), _origin);
	}
	if (!actorDrawn)
		actor->draw(screen, _origin);

	if (_showWalkMap)
		drawWalkMapOverlay(screen);
	if (_showHotspots)
		drawHotspotOverlay(screen);
	if (_showPath && actor)
		drawPathOverlay(screen, actor);
	screen->markAllDirty();
}

int World::walkRowAt(const Common::Point &pos) const {
	if (!_mapWidth)
		return -1;
	int x = MIN<int>(pos.x / kWalkCellWidth, _mapWidth - 1);
	for (uint y = 0; y < _mapHeight; y++) {
		byte cell = mapCell(x, y);
		int disp = cell & kCellDisplacementMask;
		if (cell & kCellDisplacementSign)
			disp = -disp;
		if (pos.y - disp <= _mapBaseY[y])
			return (int)y;
	}
	return (int)_mapHeight - 1;
}

void World::drawPathOverlay(Graphics::Screen *screen, const Character *actor) const {
	const Common::Array<WalkStep> &path = actor->path();
	for (uint i = actor->pathPosition(); i < path.size(); i++) {
		int sx = path[i].x - _origin.x, sy = path[i].y - _origin.y;
		if (sx >= 0 && sx < screen->w && sy >= 0 && sy < screen->h)
			*(byte *)screen->getBasePtr(sx, sy) = kOverlayWalkColor;
	}
}

void World::drawWalkMapOverlay(Graphics::Screen *screen) const {
	if (!_mapWidth)
		return;

	// Rows are bands ending at baseY; columns are kWalkCellWidth wide.
	// Walkable cells get a dotted fill.
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
