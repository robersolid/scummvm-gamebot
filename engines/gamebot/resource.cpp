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
#include "common/textconsole.h"

#include "gamebot/resource.h"
#include "gamebot/detection.h"

namespace Gamebot {

bool DataFile::open(const Common::Path &name, const char *signature, uint32 version) {
	if (!_file.open(name)) {
		warning("Could not open data file %s", name.toString().c_str());
		return false;
	}

	char fileSignature[4];
	if (_file.read(fileSignature, 4) != 4 || memcmp(fileSignature, signature, 4) != 0) {
		warning("Data file %s has an invalid signature", name.toString().c_str());
		return false;
	}

	_version = _file.readUint32LE();
	if (_version != version) {
		warning("Data file %s has unsupported version 0x%08x (expected 0x%08x)",
			name.toString().c_str(), _version, version);
		return false;
	}
	return true;
}

Common::Rect DataFile::readRect() {
	// Win32 RECT with 32-bit fields; original rects are inclusive, which
	// Common::Rect is not, hence the +1 on the bottom-right corner
	int32 left = _file.readSint32LE();
	int32 top = _file.readSint32LE();
	int32 right = _file.readSint32LE();
	int32 bottom = _file.readSint32LE();
	return Common::Rect(left, top, right + 1, bottom + 1);
}

bool ResourceFile::load(const Common::Path &name) {
	if (!open(name, "WZRS", kVersion))
		return false;

	uint32 count = _file.readUint32LE();
	_entries.resize(count);
	for (uint32 i = 0; i < count; i++) {
		ResourceEntry &e = _entries[i];
		uint32 type = _file.readUint32LE();
		e.type = (type < kResTypeCount) ? (ResourceType)type : kResUnknown;
		e.objectId = _file.readUint32LE();
		e.resId = _file.readUint32LE();
		e.size = _file.readUint32LE();
		e.location = _file.readUint32LE();
	}

	if (_file.err() || _file.eos()) {
		warning("Error reading resource index of %s", name.toString().c_str());
		return false;
	}
	debugC(kDebugResources, "%s: %u resources indexed", name.toString().c_str(), count);
	return true;
}

int ResourceFile::findObject(uint32 objectId) const {
	// The index is sorted by objectId with all entries of an object
	// contiguous, so a plain binary search plus a rewind is enough
	int low = 0, high = (int)_entries.size() - 1;
	while (low <= high) {
		int mid = (low + high) / 2;
		if (_entries[mid].objectId < objectId)
			low = mid + 1;
		else if (_entries[mid].objectId > objectId)
			high = mid - 1;
		else {
			while (mid > 0 && _entries[mid - 1].objectId == objectId)
				mid--;
			return mid;
		}
	}
	return -1;
}

const ResourceEntry *ResourceFile::findResource(uint32 objectId, ResourceType type) const {
	int i = findObject(objectId);
	if (i < 0)
		return nullptr;
	for (; i < (int)_entries.size() && _entries[i].objectId == objectId; i++) {
		if (_entries[i].type == type)
			return &_entries[i];
	}
	return nullptr;
}

byte *ResourceFile::readBlob(const ResourceEntry &e) {
	byte *data = new byte[e.size];
	_file.seek(e.location);
	if (_file.read(data, e.size) != e.size) {
		warning("Could not read resource %08x/%08x (%u bytes at %u)",
			e.objectId, e.resId, e.size, e.location);
		delete[] data;
		return nullptr;
	}
	return data;
}

bool ActionFile::load(const Common::Path &name) {
	if (!open(name, "WZAC", kVersion))
		return false;

	uint32 objectCount = _file.readUint32LE();
	uint32 ruleCount = _file.readUint32LE();

	_objects.resize(objectCount);
	for (uint32 i = 0; i < objectCount; i++) {
		_objects[i].objectId = _file.readUint32LE();
		_objects[i].firstEntry = _file.readUint32LE();
		_objects[i].lastEntry = _file.readUint32LE();
	}

	_rules.resize(ruleCount);
	for (uint32 i = 0; i < ruleCount; i++) {
		ActionRule &r = _rules[i];
		r.eventId = _file.readUint32LE();
		r.eventParam1 = _file.readUint32LE();
		r.eventParam2 = _file.readUint32LE();
		r.actionId = _file.readUint32LE();
		r.actionParam1 = _file.readUint32LE();
		r.actionParam2 = _file.readUint32LE();
		r.actionParam3 = _file.readUint32LE();
		for (uint j = 0; j < 6; j++)
			r.conditions[j] = _file.readUint16LE();
		for (uint j = 0; j < 6; j++)
			r.condArgs[j] = _file.readUint32LE();
	}

	if (_file.err() || _file.eos()) {
		warning("Error reading action tables of %s", name.toString().c_str());
		return false;
	}
	debugC(kDebugResources, "%s: %u objects, %u action rules",
		name.toString().c_str(), objectCount, ruleCount);
	return true;
}

bool ActionFile::findObject(uint32 objectId, uint &first, uint &last) const {
	for (uint i = 0; i < _objects.size(); i++) {
		if (_objects[i].objectId == objectId) {
			first = _objects[i].firstEntry;
			last = _objects[i].lastEntry;
			return first < _rules.size() && last < _rules.size() && first <= last;
		}
	}
	return false;
}

bool WorldFile::load(const Common::Path &name) {
	if (!open(name, "WZOB", kVersion))
		return false;

	uint32 phaseCount = _file.readUint32LE();
	uint32 layerTotal = _file.readUint32LE();
	uint32 objectTotal = _file.readUint32LE();

	_phases.resize(phaseCount);
	for (uint32 i = 0; i < phaseCount; i++) {
		PhaseEntry &p = _phases[i];
		p.phaseId = _file.readUint32LE();
		p.layerFirst = _file.readUint32LE();
		p.layerCount = _file.readUint32LE();
		for (uint j = 0; j < 2; j++) {
			p.chars[j].characterId = _file.readUint32LE();
			p.chars[j].x = _file.readUint16LE();
			p.chars[j].y = _file.readUint16LE();
			p.chars[j].orientation = _file.readUint16LE();
			p.chars[j].layer = _file.readUint16LE();
		}
	}

	_layers.resize(layerTotal);
	for (uint32 i = 0; i < layerTotal; i++) {
		LayerEntry &l = _layers[i];
		l.scaleFactor = _file.readUint32LE();
		l.objectFirst = _file.readUint32LE();
		l.objectCount = _file.readUint32LE();
		l.baseY = _file.readUint32LE();
	}

	_objects.resize(objectTotal);
	for (uint32 i = 0; i < objectTotal; i++) {
		ObjectEntry &o = _objects[i];
		o.objectId = _file.readUint32LE();
		o.flags = _file.readUint16LE();
		o.impossibleResponses = _file.readUint16LE();
		o.activeLayer = _file.readUint32LE();

		char nameBuffer[33] = {};
		_file.read(nameBuffer, 32);
		o.name = nameBuffer;

		o.targetX = _file.readUint16LE();
		o.targetY = _file.readUint16LE();
		o.targetOrientation = _file.readUint16LE();
		o.targetLayer = _file.readUint16LE();
		o.hotCursor = _file.readUint32LE();
	}

	if (_file.err() || _file.eos()) {
		warning("Error reading world data of %s", name.toString().c_str());
		return false;
	}
	debugC(kDebugResources, "%s: %u phases, %u layers, %u objects",
		name.toString().c_str(), phaseCount, layerTotal, objectTotal);
	return true;
}

int WorldFile::findPhase(uint32 phaseId) const {
	for (uint i = 0; i < _phases.size(); i++) {
		if (_phases[i].phaseId == phaseId)
			return (int)i;
	}
	return -1;
}

} // End of namespace Gamebot
