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

#ifndef GAMEBOT_RESOURCE_H
#define GAMEBOT_RESOURCE_H

#include "common/array.h"
#include "common/file.h"
#include "common/rect.h"
#include "common/str.h"

namespace Gamebot {

// All data files of the original engine ("BotFile") start with a 4-char
// signature followed by a version dword and per-format counters.
// All multi-byte values are little endian.

enum ResourceType {
	kResUnknown = 0,
	kResImage,               // static image: rect + 8bpp pixels
	kResHiddenImage,         // rect only, no pixels (background hotspot)
	kResMouseImage,          // rect + hotspot point + 8bpp pixels
	kResSelectImage,         // action palette: 5 rects + 4 action codes + up to 5 images
	kResAnimationAuto,       // animation, starts automatically
	kResAnimationAutoMobile, // animation with displacement, automatic
	kResAnimationEvent,      // animation triggered by an event
	kResAnimationEventMobile,
	kResInventoryImage,      // rect + two 8bpp images (normal, highlighted)
	kResPhaseInit,           // phase size + music/fx codes + 256x4 palette
	kResDialog,              // sentence count + dialog sentences
	kResSound,               // raw sound data
	kResMusic,               // streamed sound data
	kResPhaseExit,           // rect + destination phase data
	kResPhaseMap,            // walk map grid + per-row baseY/scale (see kCell*)
	kResText,                // on-screen text
	kResMapExit,             // map screen exit, same layout as kResPhaseExit
	kResFXSound,             // sound effect
	kResAnimationFlic,       // FLC video: sound code + location + size
	kResTypeCount
};

// Walk map cell bits. Note the original define for bit 7 is called
// rmProhibido ("forbidden") but the pathfinder actually treats cells
// WITH the bit set as walkable, so the original name is inverted.
// The low bits encode a per-cell Y displacement (bit 6 is its sign),
// applied to the character position while crossing the cell.
enum WalkMapCell {
	kCellWalkable = 0x80,
	kCellDisplacementSign = 0x40,
	kCellDisplacementMask = 0x3f
};

struct ResourceEntry {
	ResourceType type = kResUnknown;
	uint32 objectId = 0; // object owning the resource
	uint32 resId = 0;
	uint32 size = 0;     // size of the resource blob
	uint32 location = 0; // absolute file offset of the blob
};

// Animation blob layout, after the leading rect:
struct AnimationParams {
	uint32 imageCount = 0;
	uint32 sequenceCount = 0;
	uint32 framePeriod = 0;   // milliseconds between frames
	uint32 startPause = 0;    // milliseconds before the first frame
	uint32 imageLocation = 0; // absolute file offset of the raw frames
};

struct SequenceStep {
	// Special imageIndex values (otherwise 1-based frame index):
	static const uint32 kGotoBegin = 0xffffffff;
	static const uint32 kAutoDestroy = 0xfffffffe;
	static const uint32 kAutoDisable = 0xfffffffd;

	uint32 imageIndex = 0;
	uint32 soundCode = 0;
	uint32 textCode = 0;
};

class DataFile {
public:
	virtual ~DataFile() {}

	// Opens the file and validates signature and version
	bool open(const Common::Path &name, const char *signature, uint32 version);

	uint32 version() const { return _version; }
	Common::File &file() { return _file; }

	// Reads a 16-byte Win32 RECT (LONG left, top, right, bottom).
	// The original rects are bottom/right inclusive.
	Common::Rect readRect();

protected:
	Common::File _file;
	uint32 _version = 0;
};

// Resource container: Resdata.res and default.dlg ("WZRS")
class ResourceFile : public DataFile {
public:
	static const uint32 kVersion = 0x01000100;

	bool load(const Common::Path &name);

	uint count() const { return _entries.size(); }
	const ResourceEntry &entry(uint index) const { return _entries[index]; }

	// Index of the first entry of an object, or -1. Entries of the same
	// object are contiguous and sorted by objectId.
	int findObject(uint32 objectId) const;

	// First entry of the given object with the given type, or nullptr
	const ResourceEntry *findResource(uint32 objectId, ResourceType type) const;

	// Reads the whole blob of an entry; caller owns the buffer
	byte *readBlob(const ResourceEntry &e);

private:
	Common::Array<ResourceEntry> _entries;
};

// Action rule table: Actions.act ("WZAC")
struct ActionRule {
	uint32 eventId = 0; // triggering event
	uint32 eventParam1 = 0;
	uint32 eventParam2 = 0;
	uint32 actionId = 0; // action to perform
	uint32 actionParam1 = 0;
	uint32 actionParam2 = 0;
	uint32 actionParam3 = 0;
	uint16 conditions[6] = {};
	uint32 condArgs[6] = {};
};

class ActionFile : public DataFile {
public:
	static const uint32 kVersion = 0x01000100;

	bool load(const Common::Path &name);

	uint objectCount() const { return _objects.size(); }
	uint ruleCount() const { return _rules.size(); }

	// Rules of an object as a [first, last] range into rules(), or false
	bool findObject(uint32 objectId, uint &first, uint &last) const;
	uint32 objectIdAt(uint index) const { return _objects[index].objectId; }
	const ActionRule &rule(uint index) const { return _rules[index]; }

private:
	struct ObjectActions {
		uint32 objectId = 0;
		uint32 firstEntry = 0;
		uint32 lastEntry = 0;
	};

	Common::Array<ObjectActions> _objects;
	Common::Array<ActionRule> _rules;
};

// World state: default.def and the .def part of savegames ("WZOB").
// A phase is a room; each phase has (normally) 16 layers; each layer
// holds objects. The file is a complete dump of the world state.
struct CharacterLocation {
	uint32 characterId = 0;
	uint16 x = 0, y = 0;
	uint16 orientation = 0; // 0=north .. 7=northwest
	uint16 layer = 0;
};

struct PhaseEntry {
	uint32 phaseId = 0;
	uint32 layerFirst = 0;
	uint32 layerCount = 0;
	CharacterLocation chars[2];
};

struct LayerEntry {
	uint32 scaleFactor = 0; // 1000 = 100%
	uint32 objectFirst = 0;
	uint32 objectCount = 0;
	uint32 baseY = 0;
};

struct ObjectEntry {
	// VF capability flags
	static const uint16 kFlagTakeable = 0x0001;
	static const uint16 kFlagProcessesEvents = 0x0002;
	static const uint16 kFlagTakeFront = 0x0004;
	static const uint16 kFlagTakeAbove = 0x0008;
	static const uint16 kFlagTakeDirect = 0x0010;

	uint32 objectId = 0;
	uint16 flags = 0;         // VF_* capability flags
	uint16 impossibleResponses = 0; // TCAU sentence selection flags
	uint32 activeLayer = 0;   // layer the object joins when enabled
	Common::String name;      // 31 chars max in the file
	uint16 targetX = 0, targetY = 0; // where the character stands to use it
	uint16 targetOrientation = 0;
	uint16 targetLayer = 0;
	uint32 hotCursor = 0;
};

class WorldFile : public DataFile {
public:
	static const uint32 kVersion = 0x01000200;

	bool load(const Common::Path &name);

	uint phaseCount() const { return _phases.size(); }
	uint layerCount() const { return _layers.size(); }
	uint objectCount() const { return _objects.size(); }

	const PhaseEntry &phase(uint index) const { return _phases[index]; }
	const LayerEntry &layer(uint index) const { return _layers[index]; }
	const ObjectEntry &object(uint index) const { return _objects[index]; }

	int findPhase(uint32 phaseId) const;

private:
	Common::Array<PhaseEntry> _phases;
	Common::Array<LayerEntry> _layers;
	Common::Array<ObjectEntry> _objects;
};

} // End of namespace Gamebot

#endif // GAMEBOT_RESOURCE_H
