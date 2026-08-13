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

#include "common/endian.h"
#include "common/file.h"
#include "common/tokenizer.h"
#include "graphics/surface.h"
#include "image/bmp.h"

#include "gamebot/console.h"
#include "gamebot/debug-names.h"
#include "gamebot/gamebot.h"

namespace Gamebot {

// Ids on the command line accept decimal or 0x-prefixed hex
static uint32 parseId(const char *arg) {
	return strtoul(arg, nullptr, 0);
}

// Reads a Win32 RECT from a resource blob without adjusting for
// inclusiveness; width/height are computed with +1 where needed
struct BlobRect {
	int32 left, top, right, bottom;

	explicit BlobRect(const byte *p) {
		left = READ_LE_INT32(p);
		top = READ_LE_INT32(p + 4);
		right = READ_LE_INT32(p + 8);
		bottom = READ_LE_INT32(p + 12);
	}
	uint16 width() const { return (uint16)(right - left + 1); }
	uint16 height() const { return (uint16)(bottom - top + 1); }
	uint32 pixelCount() const { return (uint32)width() * height(); }
};

Console::Console() : GUI::Debugger() {
	// Default dump palette: grayscale identity
	for (uint i = 0; i < 256; i++)
		_dumpPalette[i * 3] = _dumpPalette[i * 3 + 1] = _dumpPalette[i * 3 + 2] = (byte)i;

	registerCmd("datafiles", WRAP_METHOD(Console, cmdDataFiles));
	registerCmd("resinfo", WRAP_METHOD(Console, cmdResInfo));
	registerCmd("phases", WRAP_METHOD(Console, cmdPhases));
	registerCmd("objects", WRAP_METHOD(Console, cmdObjects));
	registerCmd("actions", WRAP_METHOD(Console, cmdActions));
	registerCmd("dumpres", WRAP_METHOD(Console, cmdDumpRes));
	registerCmd("dumpmap", WRAP_METHOD(Console, cmdDumpMap));
	registerCmd("palette", WRAP_METHOD(Console, cmdPalette));
}

bool Console::executeCommand(const Common::String &command) {
	// Tokenize on whitespace into the argc/argv shape handleCommand expects
	Common::StringTokenizer tokenizer(command, " \t");
	Common::Array<Common::String> tokens;
	const char *argv[8];
	int argc = 0;

	while (!tokenizer.empty() && tokens.size() < ARRAYSIZE(argv))
		tokens.push_back(tokenizer.nextToken());
	for (uint i = 0; i < tokens.size(); i++)
		argv[argc++] = tokens[i].c_str();
	if (!argc)
		return false;

	bool keepRunning = true;
	debugPrintf("gamebot> %s\n", command.c_str());
	if (!handleCommand(argc, argv, keepRunning)) {
		debugPrintf("Unknown command '%s'\n", argv[0]);
		return false;
	}
	return true;
}

bool Console::cmdDataFiles(int argc, const char **argv) {
	debugPrintf("%-12s %u resources\n", GAMEBOT_RESOURCE_FILE, g_engine->resources().count());
	debugPrintf("%-12s %u objects, %u rules\n", GAMEBOT_ACTIONS_FILE,
		g_engine->actions().objectCount(), g_engine->actions().ruleCount());
	debugPrintf("%-12s %u phases, %u layers, %u objects\n", GAMEBOT_NEWGAME_FILE,
		g_engine->initialWorld().phaseCount(), g_engine->initialWorld().layerCount(),
		g_engine->initialWorld().objectCount());
	return true;
}

bool Console::cmdResInfo(int argc, const char **argv) {
	ResourceFile &res = g_engine->resources();

	if (argc < 2) {
		uint countPerType[kResTypeCount] = {};
		for (uint i = 0; i < res.count(); i++)
			countPerType[res.entry(i).type]++;
		for (uint t = 0; t < kResTypeCount; t++) {
			if (countPerType[t])
				debugPrintf("%-16s %u\n", resourceTypeName((ResourceType)t), countPerType[t]);
		}
		debugPrintf("%-16s %u\n", "total", res.count());
		debugPrintf("Use resinfo <objectId> to list the resources of one object\n");
		return true;
	}

	uint32 objectId = parseId(argv[1]);
	int i = res.findObject(objectId);
	if (i < 0) {
		debugPrintf("Object %08x has no resources\n", objectId);
		return true;
	}
	for (; i < (int)res.count() && res.entry(i).objectId == objectId; i++) {
		const ResourceEntry &e = res.entry(i);
		debugPrintf("resId %08x  %-16s %8u bytes at %u\n",
			e.resId, resourceTypeName(e.type), e.size, e.location);
	}
	return true;
}

bool Console::cmdPhases(int argc, const char **argv) {
	WorldFile &world = g_engine->initialWorld();
	for (uint i = 0; i < world.phaseCount(); i++) {
		const PhaseEntry &p = world.phase(i);
		uint objects = 0;
		for (uint l = 0; l < p.layerCount; l++)
			objects += world.layer(p.layerFirst + l).objectCount;
		debugPrintf("%08x  %2u layers %3u objects  char0 (%u,%u,or%u,L%u) char1 (%u,%u,or%u,L%u)\n",
			p.phaseId, p.layerCount, objects,
			p.chars[0].x, p.chars[0].y, p.chars[0].orientation, p.chars[0].layer,
			p.chars[1].x, p.chars[1].y, p.chars[1].orientation, p.chars[1].layer);
	}
	debugPrintf("%u phases\n", world.phaseCount());
	return true;
}

bool Console::cmdObjects(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: objects <phaseId>  (e.g. objects 0x0101)\n");
		return true;
	}

	WorldFile &world = g_engine->initialWorld();
	int phaseIndex = world.findPhase(parseId(argv[1]));
	if (phaseIndex < 0) {
		debugPrintf("Unknown phase %s\n", argv[1]);
		return true;
	}

	const PhaseEntry &p = world.phase(phaseIndex);
	for (uint l = 0; l < p.layerCount; l++) {
		const LayerEntry &layer = world.layer(p.layerFirst + l);
		if (!layer.objectCount)
			continue;
		debugPrintf("layer %2u (scale %u%%, baseY %u):\n",
			l, layer.scaleFactor / 10, layer.baseY);
		for (uint o = 0; o < layer.objectCount; o++) {
			const ObjectEntry &obj = world.object(layer.objectFirst + o);
			debugPrintf("  %08x %-32s flags %04x target (%u,%u,or%u,L%u) cursor %x\n",
				obj.objectId, obj.name.c_str(), obj.flags,
				obj.targetX, obj.targetY, obj.targetOrientation, obj.targetLayer,
				obj.hotCursor);
		}
	}
	return true;
}

bool Console::cmdActions(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: actions <objectId>\n");
		return true;
	}

	ActionFile &actions = g_engine->actions();
	uint first, last;
	if (!actions.findObject(parseId(argv[1]), first, last)) {
		debugPrintf("Object %s has no action rules\n", argv[1]);
		return true;
	}

	for (uint i = first; i <= last; i++) {
		const ActionRule &r = actions.rule(i);
		const char *evName = eventName(r.eventId);
		const char *actName = actionName(r.actionId);
		debugPrintf("on %s(%x,%x) do %s(%x,%x,%x)",
			evName ? evName : Common::String::format("event%08x", r.eventId).c_str(),
			r.eventParam1, r.eventParam2,
			actName ? actName : Common::String::format("action%08x", r.actionId).c_str(),
			r.actionParam1, r.actionParam2, r.actionParam3);
		for (uint c = 0; c < 6; c++) {
			if (r.conditions[c])
				debugPrintf(" [%s %x]", conditionName(r.conditions[c]).c_str(), r.condArgs[c]);
		}
		debugPrintf("\n");
	}
	debugPrintf("%u rules\n", last - first + 1);
	return true;
}

bool Console::dumpSurface(const byte *pixels, uint16 width, uint16 height,
		const Common::String &fileName) {
	Graphics::Surface surface;
	surface.create(width, height, Graphics::PixelFormat::createFormatCLUT8());
	for (uint16 y = 0; y < height; y++)
		memcpy(surface.getBasePtr(0, y), pixels + (uint32)y * width, width);

	Common::DumpFile out;
	bool result = out.open(Common::Path(fileName), true) &&
		Image::writeBMP(out, surface, _dumpPalette, 256);
	surface.free();

	if (result)
		debugPrintf("Wrote %s (%ux%u)\n", fileName.c_str(), width, height);
	else
		debugPrintf("Could not write %s\n", fileName.c_str());
	return result;
}

// Decodes the images inside a resource blob into BMP dumps.
// Returns the number of files written.
uint Console::dumpImageBlob(const ResourceEntry &e, const byte *data) {
	const Common::String base = Common::String::format(
		"gamebot-dumps/%08x-%08x", e.objectId, e.resId);
	uint written = 0;

	switch (e.type) {
	case kResImage: {
		BlobRect rect(data);
		if (dumpSurface(data + 16, rect.width(), rect.height(), base + ".bmp"))
			written++;
		break;
	}
	case kResMouseImage: {
		BlobRect rect(data);
		debugPrintf("Cursor hotspot: (%u, %u)\n",
			READ_LE_UINT32(data + 16), READ_LE_UINT32(data + 20));
		if (dumpSurface(data + 24, rect.width(), rect.height(), base + ".bmp"))
			written++;
		break;
	}
	case kResInventoryImage: {
		BlobRect rect(data);
		if (dumpSurface(data + 16, rect.width(), rect.height(), base + "-normal.bmp"))
			written++;
		if (dumpSurface(data + 16 + rect.pixelCount(), rect.width(), rect.height(),
				base + "-highlight.bmp"))
			written++;
		break;
	}
	case kResSelectImage: {
		// 5 rects, then 4 action codes, then the images of every rect
		// whose bottom coordinate is not zero
		uint32 pos = 5 * 16 + 4 * 4;
		for (uint n = 0; n < 5; n++) {
			BlobRect rect(data + n * 16);
			if (!rect.bottom)
				continue;
			if (dumpSurface(data + pos, rect.width(), rect.height(),
					Common::String::format("%s-%u.bmp", base.c_str(), n)))
				written++;
			pos += rect.pixelCount();
		}
		break;
	}
	case kResAnimationAuto:
	case kResAnimationAutoMobile:
	case kResAnimationEvent:
	case kResAnimationEventMobile: {
		BlobRect rect(data);
		AnimationParams params;
		params.imageCount = READ_LE_UINT32(data + 16);
		params.sequenceCount = READ_LE_UINT32(data + 20);
		params.framePeriod = READ_LE_UINT32(data + 24);
		params.startPause = READ_LE_UINT32(data + 28);
		params.imageLocation = READ_LE_UINT32(data + 32);
		debugPrintf("Animation: %u frames, %u sequence steps, %u ms/frame\n",
			params.imageCount, params.sequenceCount, params.framePeriod);

		const uint32 frameSize = rect.pixelCount();
		byte *frame = new byte[frameSize];
		Common::File &file = g_engine->resources().file();
		for (uint32 i = 0; i < params.imageCount; i++) {
			file.seek(params.imageLocation + i * frameSize);
			if (file.read(frame, frameSize) != frameSize) {
				debugPrintf("Could not read frame %u\n", i);
				break;
			}
			if (dumpSurface(frame, rect.width(), rect.height(),
					Common::String::format("%s-f%02u.bmp", base.c_str(), i)))
				written++;
		}
		delete[] frame;
		break;
	}
	default:
		break;
	}
	return written;
}

bool Console::cmdDumpRes(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: dumpres <objectId> [resId]\n");
		debugPrintf("Dumps to gamebot-dumps/ in the current directory.\n");
		debugPrintf("Images become BMPs (see the palette command), the rest raw .bin files.\n");
		return true;
	}

	ResourceFile &res = g_engine->resources();
	uint32 objectId = parseId(argv[1]);
	bool singleRes = argc > 2;
	uint32 resId = singleRes ? parseId(argv[2]) : 0;

	int i = res.findObject(objectId);
	if (i < 0) {
		debugPrintf("Object %08x has no resources\n", objectId);
		return true;
	}

	for (; i < (int)res.count() && res.entry(i).objectId == objectId; i++) {
		const ResourceEntry &e = res.entry(i);
		if (singleRes && e.resId != resId)
			continue;

		byte *data = res.readBlob(e);
		if (!data) {
			debugPrintf("Could not read resource %08x/%08x\n", e.objectId, e.resId);
			continue;
		}

		if (!dumpImageBlob(e, data)) {
			// Not an image (or decoding failed): dump the raw blob
			Common::String fileName = Common::String::format(
				"gamebot-dumps/%08x-%08x-%s.bin", e.objectId, e.resId,
				resourceTypeName(e.type));
			Common::DumpFile out;
			if (out.open(Common::Path(fileName), true) && out.write(data, e.size) == e.size)
				debugPrintf("Wrote %s (%u bytes)\n", fileName.c_str(), e.size);
			else
				debugPrintf("Could not write %s\n", fileName.c_str());
		}
		delete[] data;
	}
	return true;
}

bool Console::cmdDumpMap(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: dumpmap <phaseId>\n");
		return true;
	}

	// Walk maps are owned by the characters (e.g. object 0099aa99 for
	// Mortadelo), one per phase, with the phase id in the resId low word
	ResourceFile &res = g_engine->resources();
	uint32 phaseId = parseId(argv[1]);
	const ResourceEntry *e = nullptr;
	for (uint i = 0; i < res.count(); i++) {
		if (res.entry(i).type == kResPhaseMap && (res.entry(i).resId & 0xffff) == phaseId) {
			e = &res.entry(i);
			break;
		}
	}
	if (!e) {
		debugPrintf("Phase %08x has no walk map\n", phaseId);
		return true;
	}
	debugPrintf("Walk map of object %08x, resId %08x\n", e->objectId, e->resId);

	byte *data = res.readBlob(*e);
	if (!data)
		return true;

	uint32 width = READ_LE_UINT32(data);
	uint32 height = READ_LE_UINT32(data + 4);
	const byte *baseY = data + 8;
	const byte *scale = baseY + height * 2;
	const byte *cells = scale + height * 2;

	debugPrintf("Walk map %ux%u cells\n", width, height);
	debugPrintf("'#' walkable, '.' blocked; with y-displacement: '+'/'-' walkable, 'p'/'n' blocked\n");
	for (uint32 y = 0; y < height; y++) {
		Common::String row;
		for (uint32 x = 0; x < width; x++) {
			byte cell = cells[y * width + x];
			bool walkable = (cell & kCellWalkable) != 0;
			if (!(cell & kCellDisplacementMask))
				row += walkable ? '#' : '.';
			else if (cell & kCellDisplacementSign)
				row += walkable ? '-' : 'n';
			else
				row += walkable ? '+' : 'p';
		}
		debugPrintf("%3u %s  baseY=%u scale=%u%%\n", y, row.c_str(),
			READ_LE_UINT16(baseY + y * 2), READ_LE_UINT16(scale + y * 2) / 10);
	}
	delete[] data;
	return true;
}

bool Console::cmdPalette(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: palette <phaseId>\n");
		debugPrintf("Loads the phase palette for following dumpres commands.\n");
		return true;
	}

	ResourceFile &res = g_engine->resources();
	uint32 phaseId = parseId(argv[1]);
	const ResourceEntry *e = res.findResource(phaseId, kResPhaseInit);
	if (!e) {
		debugPrintf("Phase %08x has no phase init resource\n", phaseId);
		return true;
	}

	byte *data = res.readBlob(*e);
	if (!data)
		return true;

	// Layout: phase size (2x uint32), music code, fx code, then the
	// palette as 256 PALETTEENTRY structs (red, green, blue, flags)
	debugPrintf("Phase %08x: size %ux%u, music %x, fx %x\n", phaseId,
		READ_LE_UINT32(data), READ_LE_UINT32(data + 4),
		READ_LE_UINT32(data + 8), READ_LE_UINT32(data + 12));
	for (uint i = 0; i < 256; i++) {
		_dumpPalette[i * 3] = data[16 + i * 4];
		_dumpPalette[i * 3 + 1] = data[16 + i * 4 + 1];
		_dumpPalette[i * 3 + 2] = data[16 + i * 4 + 2];
	}
	delete[] data;
	debugPrintf("Dump palette set from phase %08x\n", phaseId);
	return true;
}

} // End of namespace Gamebot
