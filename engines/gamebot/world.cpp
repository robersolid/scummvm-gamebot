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

// Reads an inclusive bottom-right rect from a resource blob. A few
// resources in the data carry swapped corners (e.g. the TIA barracks
// hidden hotspot); the original never matches those rects, so they
// degrade to an empty rect here instead of asserting.
static Common::Rect readBlobRect(const byte *data) {
	int32 left = READ_LE_INT32(data), top = READ_LE_INT32(data + 4);
	int32 right = READ_LE_INT32(data + 8) + 1, bottom = READ_LE_INT32(data + 12) + 1;
	if (right < left || bottom < top)
		return Common::Rect(left, top, left, top);
	return Common::Rect(left, top, right, bottom);
}

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
	for (uint i = 0; i < 256; i++) {
		_palette[i * 3] = data[16 + i * 4];
		_palette[i * 3 + 1] = data[16 + i * 4 + 1];
		_palette[i * 3 + 2] = data[16 + i * 4 + 2];
	}
	applyPalette();

	_musicCode = READ_LE_UINT32(data + 8);
	debugC(kDebugResources, "Phase %08x: %dx%d, music %x, fx %x", phaseId,
		_phaseWidth, _phaseHeight, _musicCode, READ_LE_UINT32(data + 12));

	// The phase activation starts its weather effect, exactly as the
	// original VisualPhase posts the fx-start on evAppActivate; the
	// script can still change it through the same message
	_fxCode = READ_LE_UINT32(data + 12);
	initWeather();
	g_engine->sounds().stopAll();
	if (_musicCode)
		g_engine->sounds().playMusic(_musicCode);
	else
		g_engine->sounds().stopMusic();
	delete[] data;
	return true;
}

void World::applyPalette() const {
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
}

// Weather constants of the original FXMaster
enum {
	kFxSnow = 1,
	kFxRain = 2,
	kSnowColor = 19,        // palette entries chosen by the artists
	kRainColor = 17,
	kBigFlake = 7,
	kSmallFlake = 5,
	kRainDrop = 17,
	kFxItemCount = 100,
	kFxTickMs = 75,
	kFxRandomSeed = 23637
};

// A snow flake is a rounded square: the corner rows are inset
static void drawFlake(byte *buffer, uint32 origin, byte color, uint32 size) {
	uint32 end = size;
	if (end + origin / kScreenWidth > kScreenHeight)
		end = kScreenHeight - origin / kScreenWidth;
	for (uint32 i = 0; i < end; i++) {
		uint32 inset = 0;
		if (i == 0 || i == size - 1)
			inset = 1 + (size == kBigFlake);
		if (i == 1 || i == size - 2)
			inset = (size == kBigFlake) ? 1 : 0;
		for (uint32 j = inset; j < size - inset; j++)
			buffer[origin + kScreenWidth * i + j] = color;
	}
}

// A rain drop is a slightly slanted dashed streak
static void drawDrop(byte *buffer, uint32 origin, byte color, uint32 size) {
	if (size + origin / kScreenWidth > kScreenHeight)
		size = kScreenHeight - origin / kScreenWidth;
	for (uint32 i = 0, j = 0; i < size; i++) {
		if (i == 4 || i == 8 || i == 12 || i == 16)
			j++;
		if (i != 1 && i != 3)
			buffer[origin + kScreenWidth * i + j] = color;
	}
}

void World::initWeather() {
	_fxNextTick = 0;
	if (!_fxCode) {
		_fxBuffer.clear();
		_fxItems.clear();
		return;
	}
	_fxBuffer.resize(kScreenWidth * kScreenHeight);
	memset(_fxBuffer.data(), kTransparentColor, _fxBuffer.size());

	// Deterministic initial spread, as seeded in the original
	_fxItems.resize(kFxItemCount);
	uint32 random = kFxRandomSeed;
	for (uint i = 0; i < kFxItemCount; i++) {
		_fxItems[i] = random;
		random += 13;
		random = (random * 37) % (kScreenWidth * kScreenHeight - kBigFlake);
	}
}

void World::updateWeather(uint32 millis) {
	if (!_fxCode || _fxBuffer.empty())
		return;
	if (!_fxNextTick)
		_fxNextTick = millis + kFxTickMs;
	byte *buffer = _fxBuffer.data();

	while (millis >= _fxNextTick) {
		_fxNextTick += kFxTickMs;
		if (_fxCode == kFxSnow) {
			// Big flakes fall four rows per tick, small ones two,
			// both with a small horizontal jitter
			for (int i = kFxItemCount - 1; i >= kFxItemCount / 2; i--) {
				drawFlake(buffer, _fxItems[i], kTransparentColor, kBigFlake);
				_fxItems[i] = (_fxItems[i] + kScreenWidth * 4 +
					g_engine->getRandomNumber(3) - g_engine->getRandomNumber(3)) %
					(kScreenWidth * kScreenHeight - kBigFlake);
				drawFlake(buffer, _fxItems[i], kSnowColor, kBigFlake);
			}
			for (int i = kFxItemCount / 2 - 1; i >= 0; i--) {
				drawFlake(buffer, _fxItems[i], kTransparentColor, kSmallFlake);
				_fxItems[i] = (_fxItems[i] + kScreenWidth * 2 +
					g_engine->getRandomNumber(3) - g_engine->getRandomNumber(3)) %
					(kScreenWidth * kScreenHeight - kSmallFlake);
				drawFlake(buffer, _fxItems[i], kSnowColor, kSmallFlake);
			}
		} else if (_fxCode == kFxRain) {
			// Drops fall 25 rows and drift 4 pixels per tick. The
			// original loop also read one item past the table; that
			// overrun is not reproduced.
			for (int i = kFxItemCount - 1; i >= 0; i--) {
				drawDrop(buffer, _fxItems[i], kTransparentColor, kRainDrop);
				_fxItems[i] = (_fxItems[i] + kScreenWidth * 25) %
					(kScreenWidth * kScreenHeight) + 4;
				if (_fxItems[i] + kScreenWidth * kRainDrop >= kScreenWidth * kScreenHeight)
					_fxItems[i] %= kScreenWidth;
				drawDrop(buffer, _fxItems[i], kRainColor, kRainDrop);
			}
		}
	}
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
				hotspot.layer = layer;
				hotspot.name = object.name;
				hotspot.type = e.type;
				hotspot.rect = readBlobRect(data);
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
			item.rect = readBlobRect(data);
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
	anim.rect = readBlobRect(data);
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

	// Objects waiting in layer 0 load hidden at their active layer, so
	// an enable can reveal them (the original moves them between the
	// disabled layer and the one recorded in the object entry). They
	// are merged into the descending-layer draw order.
	if (phase.layerCount > 0) {
		const LayerEntry &disabled = world.layer(phase.layerFirst);
		uint firstHidden = _items.size();
		for (uint o = 0; o < disabled.objectCount; o++) {
			const ObjectEntry &object = world.object(disabled.objectFirst + o);
			uint16 layer = (uint16)CLIP<uint32>(object.activeLayer, 1, phase.layerCount - 1);
			uint before = _items.size();
			uint hotspotsBefore = _hotspots.size();
			addDrawItem(object, layer);
			for (uint i = before; i < _items.size(); i++)
				_items[i].visible = false;
			for (uint i = hotspotsBefore; i < _hotspots.size(); i++)
				_hotspots[i].enabled = false;
		}
		for (uint i = firstHidden; i < _items.size(); i++) {
			DrawItem item = _items[i];
			_items.remove_at(i);
			uint pos = 0;
			while (pos < i && _items[pos].layer >= item.layer)
				pos++;
			_items.insert_at(pos, item);
		}
	}

	debugC(kDebugResources, "Phase %08x loaded: %u draw items, %u hotspots, map %ux%u",
		phaseId, _items.size(), _hotspots.size(), _mapWidth, _mapHeight);
	return true;
}

// Plays the sound and shows the text a sequence step carries
void World::emitStepEffects(const DrawItem &item, const Animation &anim) const {
	if (anim.seqPos >= anim.sequence.size())
		return;
	const SequenceStep &step = anim.sequence[anim.seqPos];
	if (step.soundCode)
		g_engine->sounds().playSound(step.soundCode, Audio::Mixer::kSFXSoundType, item.objectId);
	if (step.textCode)
		g_engine->logic().writer().showTextCode(step.textCode);
}

void World::updateItem(DrawItem &item, uint32 millis) {
	// The original object has a single active resource: while any
	// animation runs (automatic or event), the pending ones wait for
	// it to end, in resource order. A paused object (listening to a
	// phrase or holding a conversation) arms no automatic animation,
	// but a running event animation still plays.
	bool animRunning = item.activeAnim >= 0 && item.anims[item.activeAnim].running;

	// Start whichever animation reaches its fire time; a firing
	// animation becomes the active resource of the object
	for (uint a = 0; a < item.anims.size() && !animRunning && !item.animsPaused; a++) {
		Animation &anim = item.anims[a];
		if (anim.running || !anim.autoFire)
			continue;
		// A pause-less animation becomes the active resource on the
		// activation itself (original StartAnimation), so the static
		// image never flashes before it
		if (!anim.fireTime)
			anim.fireTime = millis + anim.params.startPause;
		if (millis >= anim.fireTime) {
			anim.running = true;
			anim.seqPos = 0;
			anim.stepTime = millis + anim.params.framePeriod;
			item.activeAnim = (int)a;
			item.curDeltaX = item.curDeltaY = 0;
			debugC(2, kDebugEvents, "Animation %08x/%08x starts",
				item.objectId, anim.resId);
			emitStepEffects(item, anim);
			break; // one active resource at a time
		}
	}

	if (item.activeAnim < 0)
		return;
	Animation &anim = item.anims[item.activeAnim];
	if (!anim.running || (item.animsPaused && anim.autoFire))
		return;

	while (millis >= anim.stepTime && anim.running) {
		anim.stepTime += anim.params.framePeriod ? anim.params.framePeriod : 100;
		anim.seqPos++;

		uint32 imageIndex = (anim.seqPos < anim.sequence.size())
			? anim.sequence[anim.seqPos].imageIndex : 0;

		if (imageIndex == SequenceStep::kGotoBegin) {
			anim.seqPos = 0;
			imageIndex = anim.sequence[0].imageIndex;
		} else if (imageIndex == SequenceStep::kAutoDestroy ||
				imageIndex == SequenceStep::kAutoDisable) {
			// Auto-destroy/disable markers end the animation for good:
			// the object hides (or reverts to its static image) and
			// the animation never rearms
			anim.running = false;
			anim.autoFire = false;
			anim.seqPos = 0;
			if (item.staticPixels) {
				item.activeAnim = -1;
				item.curDeltaX = item.curDeltaY = 0;
			} else if (imageIndex == SequenceStep::kAutoDestroy) {
				item.visible = false;
			}
			debugC(2, kDebugEvents, "Animation %08x/%08x self-disables",
				item.objectId, anim.resId);
			anim.notifyEnd = false;
			g_engine->logic().onAnimationEnded(anim.resId);
			break;
		} else if (imageIndex == 0) {
			// Plain end of a non-cyclic sequence: show the idle look,
			// rearm the clock (ambient animations replay after their
			// pause) and let the rule tables see the end
			anim.running = false;
			anim.seqPos = 0;
			anim.fireTime = millis + anim.params.startPause;
			if (item.staticPixels) {
				item.activeAnim = -1;
				item.curDeltaX = item.curDeltaY = 0;
			}
			debugC(2, kDebugEvents, "Animation %08x/%08x ends",
				item.objectId, anim.resId);
			anim.notifyEnd = false;
			g_engine->logic().onAnimationEnded(anim.resId);
			break;
		}

		item.curDeltaX = (int16)(anim.seqPos * anim.stepDeltaX);
		item.curDeltaY = (int16)(anim.seqPos * anim.stepDeltaY);
		debugC(3, kDebugEvents, "Animation %08x/%08x step %u frame %u",
			item.objectId, anim.resId, anim.seqPos, imageIndex);

		emitStepEffects(item, anim);
	}
}

void World::update(uint32 millis) {
	for (uint i = 0; i < _items.size(); i++) {
		if (!_items[i].anims.empty() && _items[i].visible)
			updateItem(_items[i], millis);
	}
	updateWeather(millis);
}

void World::pauseObjectAnims(uint32 objectId, bool paused) {
	for (uint i = 0; i < _items.size(); i++) {
		DrawItem &item = _items[i];
		if (item.objectId != objectId)
			continue;
		item.animsPaused = paused;
		if (paused) {
			// The object drops back to its base pose while it listens
			for (uint a = 0; a < item.anims.size(); a++) {
				if (!item.anims[a].autoFire)
					continue;
				item.anims[a].running = false;
				item.anims[a].seqPos = 0;
			}
			if (item.activeAnim >= 0 && item.anims[item.activeAnim].autoFire) {
				item.activeAnim = -1;
				item.curDeltaX = item.curDeltaY = 0;
			}
		} else {
			// Ambient animations rearm with their own start pauses
			uint32 millis = g_system->getMillis();
			for (uint a = 0; a < item.anims.size(); a++)
				if (item.anims[a].autoFire)
					item.anims[a].fireTime = millis + item.anims[a].params.startPause;
		}
		return;
	}
}

bool World::hasObject(uint32 objectId) const {
	for (uint i = 0; i < _items.size(); i++) {
		if (_items[i].objectId == objectId)
			return true;
	}
	for (uint i = 0; i < _hotspots.size(); i++) {
		if (_hotspots[i].objectId == objectId)
			return true;
	}
	return false;
}

bool World::isEnabled(uint32 objectId) const {
	for (uint i = 0; i < _items.size(); i++) {
		if (_items[i].objectId == objectId)
			return _items[i].visible;
	}
	for (uint i = 0; i < _hotspots.size(); i++) {
		if (_hotspots[i].objectId == objectId)
			return _hotspots[i].enabled;
	}
	return false;
}

void World::setEnabled(uint32 objectId, bool enabled) {
	bool found = false;
	// The interaction areas follow the object in and out of layer 0
	for (uint i = 0; i < _hotspots.size(); i++) {
		if (_hotspots[i].objectId == objectId) {
			_hotspots[i].enabled = enabled;
			found = true;
		}
	}
	// The original moves disabled objects to layer 0; visibility is
	// the observable effect for both drawing and hit tests
	for (uint i = 0; i < _items.size(); i++) {
		if (_items[i].objectId == objectId) {
			_items[i].visible = enabled;
			if (!enabled) {
				g_engine->sounds().stopSoundByObject(objectId);
				if (_items[i].activeAnim >= 0) {
					_items[i].anims[_items[i].activeAnim].running = false;
					_items[i].activeAnim = -1;
				}
			}
			found = true;
		}
	}
	if (found) {
		debugC(kDebugActions, "Object %08x %s", objectId, enabled ? "enabled" : "disabled");
	} else {
		debugC(kDebugActions, "Object %08x not in this phase (%s ignored)",
			objectId, enabled ? "enable" : "disable");
	}
}

// Jumps the running event animation to its end (fast-forward while
// the player skips through a scripted scene); the end notification
// fires as if it had played out
bool World::skipEventAnimation() {
	for (uint i = 0; i < _items.size(); i++) {
		DrawItem &item = _items[i];
		if (item.activeAnim < 0)
			continue;
		Animation &anim = item.anims[item.activeAnim];
		if (!anim.running || !anim.notifyEnd)
			continue;
		
		g_engine->sounds().stopSoundByObject(item.objectId);
		uint32 resId = anim.resId;
		anim.running = false;
		anim.seqPos = 0;
		item.activeAnim = -1;
		item.curDeltaX = item.curDeltaY = 0;
		uint32 millis = g_system->getMillis();
		for (uint a = 0; a < item.anims.size(); a++)
			if (item.anims[a].autoFire)
				item.anims[a].fireTime = millis + item.anims[a].params.startPause;
		debugC(kDebugEvents, "Animation %08x/%08x skipped", item.objectId, resId);
		g_engine->logic().onAnimationEnded(resId);
		return true;
	}
	return false;
}

bool World::startAnimation(uint32 objectId, uint32 resId) {
	if (!g_engine->logic().isObjectEnabled(objectId))
		return false;

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
			emitStepEffects(item, anim);
			return true;
		}
	}
	debugC(kDebugActions, "Animation %08x/%08x not available in this phase", objectId, resId);
	return false;
}

bool World::hitTest(const Common::Point &pos, HitResult &result) const {
	// Test from front-most layer (1) to back-most layer (N)
	uint16 minLayer = 1;
	uint16 maxLayer = 1;
	for (uint i = 0; i < _items.size(); i++) {
		if (_items[i].layer > maxLayer)
			maxLayer = _items[i].layer;
	}
	for (uint i = 0; i < _hotspots.size(); i++) {
		if (_hotspots[i].layer > maxLayer)
			maxLayer = _hotspots[i].layer;
	}

	for (uint16 l = minLayer; l <= maxLayer; l++) {
		// Test items in layer l (front to back within the layer)
		for (int i = (int)_items.size() - 1; i >= 0; i--) {
			const DrawItem &item = _items[i];
			if (item.layer != l)
				continue;
			Common::Rect r = item.currentRect();
			if (!item.visible || !r.contains(pos))
				continue;
			const byte *pixels = item.currentPixels();
			if (!pixels)
				continue;
			byte pixel = pixels[(pos.y - r.top) * r.width() + (pos.x - r.left)];
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

		// Test hotspots in layer l (front to back within the layer)
		for (int i = (int)_hotspots.size() - 1; i >= 0; i--) {
			const Hotspot &h = _hotspots[i];
			if (h.layer != l || !h.enabled || !h.rect.contains(pos))
				continue;
			result.objectId = h.objectId;
			result.name = h.name;
			result.type = h.type;
			result.exitPhase = h.exitPhase;
			return true;
		}
	}

	return false;
}

bool World::hitTestObject(uint32 objectId, const Common::Point &pos) const {
	for (int i = (int)_hotspots.size() - 1; i >= 0; i--) {
		if (_hotspots[i].objectId == objectId && _hotspots[i].enabled && _hotspots[i].rect.contains(pos))
			return true;
	}

	for (int i = (int)_items.size() - 1; i >= 0; i--) {
		const DrawItem &item = _items[i];
		if (item.objectId != objectId || !item.visible)
			continue;
		Common::Rect r = item.currentRect();
		if (!r.contains(pos))
			continue;
		const byte *pixels = item.currentPixels();
		if (!pixels)
			continue;
		byte pixel = pixels[(pos.y - r.top) * r.width() + (pos.x - r.left)];
		if (pixel != kTransparentColor)
			return true;
	}

	return false;
}

// Blits an 8bpp image at absolute phase coordinates onto the screen,
// honoring the scroll origin, clipping and the transparent color
static void blitItem(Graphics::Screen *screen, const byte *pixels,
		const Common::Rect &rect, const Common::Point &origin) {
	if (!pixels)
		return;
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
		for (int x = 0; x < clipped.width(); x++) {
			if (src[x] != kTransparentColor)
				dst[x] = src[x];
		}
	}
}

void World::draw(Graphics::Screen *screen, const Character *actor, const Character *partner) {
	// Draw background color (color 0 in palette is transparent, 1 is black)
	screen->clear(kTransparentColor);

	// Characters layer alongside the scene items: within
	// a layer the one lower on screen draws in front.
	const Character *actors[2] = { actor, partner };
	if (actor && partner) {
		if (partner->layer() > actor->layer() ||
				(partner->layer() == actor->layer() && partner->y() < actor->y())) {
			actors[0] = partner;
			actors[1] = actor;
		}
	}
	bool drawn[2];
	for (uint a = 0; a < 2; a++) {
		drawn[a] = !actors[a] || !actors[a]->isLoaded() || !actors[a]->visible;
	}

	for (uint i = 0; i < _items.size(); i++) {
		for (uint a = 0; a < 2; a++) {
			if (!drawn[a] && _items[i].layer < actors[a]->layer()) {
				actors[a]->draw(screen, _origin);
				drawn[a] = true;
			}
		}
		if (_items[i].visible)
			blitItem(screen, _items[i].currentPixels(), _items[i].currentRect(), _origin);
	}
	for (uint a = 0; a < 2; a++) {
		if (!drawn[a])
			actors[a]->draw(screen, _origin);
	}

	// Weather falls over the whole scene, in screen space
	if (_fxCode && !_fxBuffer.empty()) {
		const byte *fx = _fxBuffer.data();
		for (int y = 0; y < screen->h; y++) {
			byte *dst = (byte *)screen->getBasePtr(0, y);
			const byte *src = fx + y * kScreenWidth;
			for (int x = 0; x < screen->w; x++) {
				if (src[x] != kTransparentColor)
					dst[x] = src[x];
			}
		}
	}

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
