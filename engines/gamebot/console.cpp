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

#include "audio/audiostream.h"
#include "audio/decoders/adpcm.h"
#include "common/endian.h"
#include "common/substream.h"
#include "video/flic_decoder.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/system.h"
#include "common/tokenizer.h"
#include "graphics/paletteman.h"
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
	registerCmd("goto", WRAP_METHOD(Console, cmdGoto));
	registerCmd("scroll", WRAP_METHOD(Console, cmdScroll));
	registerCmd("screenshot", WRAP_METHOD(Console, cmdScreenshot));
	registerCmd("showwalkmap", WRAP_METHOD(Console, cmdOverlay));
	registerCmd("showhotspots", WRAP_METHOD(Console, cmdOverlay));
	registerCmd("showpath", WRAP_METHOD(Console, cmdOverlay));
	registerCmd("wait", WRAP_METHOD(Console, cmdWait));
	registerCmd("walk", WRAP_METHOD(Console, cmdWalk));
	registerCmd("tp", WRAP_METHOD(Console, cmdTeleport));
	registerCmd("charinfo", WRAP_METHOD(Console, cmdCharInfo));
	registerCmd("verb", WRAP_METHOD(Console, cmdVerb));
	registerCmd("inv", WRAP_METHOD(Console, cmdInventory));
	registerCmd("say", WRAP_METHOD(Console, cmdSay));
	registerCmd("dialog", WRAP_METHOD(Console, cmdDialog));
	registerCmd("pick", WRAP_METHOD(Console, cmdPick));
	registerCmd("click", WRAP_METHOD(Console, cmdClick));
	registerCmd("rclick", WRAP_METHOD(Console, cmdClick));
	registerCmd("usewith", WRAP_METHOD(Console, cmdUseWith));
	registerCmd("dumpsound", WRAP_METHOD(Console, cmdDumpSound));
	registerCmd("playflic", WRAP_METHOD(Console, cmdPlayFlic));
	registerCmd("dumpflic", WRAP_METHOD(Console, cmdDumpFlic));
	registerCmd("saveslot", WRAP_METHOD(Console, cmdSaveSlot));
	registerCmd("loadslot", WRAP_METHOD(Console, cmdSaveSlot));
	registerCmd("master", WRAP_METHOD(Console, cmdMaster));
	registerCmd("weather", WRAP_METHOD(Console, cmdWeather));
	registerCmd("exitgame", WRAP_METHOD(Console, cmdExitGame));
}

// Ends the process cleanly so scripted validation runs need no
// external timeout
bool Console::cmdExitGame(int argc, const char **argv) {
	g_engine->quitGame();
	return true;
}

bool Console::cmdWeather(int argc, const char **argv) {
	if (argc > 1)
		g_engine->world().setWeather(strtoul(argv[1], nullptr, 0));
	debugPrintf("Weather = %u (0 off, 1 snow, 2 rain)\n", g_engine->world().weather());
	return true;
}

bool Console::cmdMaster(int argc, const char **argv) {
	g_engine->switchMaster();
	debugPrintf("Master is now %08x\n", g_engine->master().objectId());
	return true;
}

bool Console::cmdSaveSlot(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: %s <slot>\n", argv[0]);
		return true;
	}
	int slot = atoi(argv[1]);
	Common::Error result = !strcmp(argv[0], "saveslot")
		? g_engine->saveGameState(slot, "debug save")
		: g_engine->loadGameState(slot);
	debugPrintf("%s slot %d: %s\n", argv[0], slot,
		result.getCode() == Common::kNoError ? "ok" : result.getDesc().c_str());
	return true;
}

bool Console::cmdPlayFlic(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: playflic <resId>\n");
		return true;
	}
	g_engine->playVideo(parseId(argv[1]));
	return true;
}

// Decodes one frame of a video to a BMP for validation
bool Console::cmdDumpFlic(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: dumpflic <resId> [frame]\n");
		return true;
	}

	ResourceFile &res = g_engine->resources();
	const ResourceEntry *e = res.findByResId(parseId(argv[1]));
	if (!e || e->type != kResAnimationFlic) {
		debugPrintf("Video not found\n");
		return true;
	}
	byte *params = res.readBlob(*e);
	if (!params)
		return true;
	uint32 flicLocation = READ_LE_UINT32(params + 4);
	uint32 flicSize = READ_LE_UINT32(params + 8);
	delete[] params;

	Common::File *file = new Common::File();
	if (!file->open(GAMEBOT_RESOURCE_FILE)) {
		delete file;
		return true;
	}
	Video::FlicDecoder decoder;
	if (!decoder.loadStream(new Common::SeekableSubReadStream(
			file, flicLocation, flicLocation + flicSize, DisposeAfterUse::YES))) {
		debugPrintf("Not a valid FLC\n");
		return true;
	}

	uint targetFrame = (argc > 2) ? strtoul(argv[2], nullptr, 0) : 1;
	const Graphics::Surface *frame = nullptr;
	for (uint i = 0; i < targetFrame && !decoder.endOfVideo(); i++)
		frame = decoder.decodeNextFrame();
	if (!frame) {
		debugPrintf("Could not decode frame %u\n", targetFrame);
		return true;
	}

	Common::String fileName = Common::String::format(
		"gamebot-dumps/flic-%08x-f%u.bmp", e->resId, targetFrame);
	Common::DumpFile out;
	if (out.open(Common::Path(fileName), true) &&
			Image::writeBMP(out, *frame, decoder.getPalette(), 256))
		debugPrintf("Wrote %s (%u of %u frames, %ux%u)\n", fileName.c_str(),
			targetFrame, decoder.getFrameCount(), frame->w, frame->h);
	return true;
}

// Decodes an ADPCM sound to a PCM wav file for validation
bool Console::cmdDumpSound(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: dumpsound <resId>\n");
		return true;
	}

	ResourceFile &res = g_engine->resources();
	const ResourceEntry *e = res.findByResId(parseId(argv[1]));
	if (!e || (e->type != kResSound && e->type != kResFXSound && e->type != kResMusic)) {
		debugPrintf("Sound resource not found\n");
		return true;
	}
	byte *data = res.readBlob(*e);
	if (!data)
		return true;

	Common::MemoryReadStream *memory =
		new Common::MemoryReadStream(data, e->size, DisposeAfterUse::YES);
	Audio::RewindableAudioStream *stream = Audio::makeADPCMStream(
		memory, DisposeAfterUse::YES, e->size, Audio::kADPCMMS, 22050, 1, 512);

	Common::Array<int16> samples;
	int16 buffer[4096];
	int got;
	while ((got = stream->readBuffer(buffer, 4096)) > 0) {
		for (int i = 0; i < got; i++)
			samples.push_back(buffer[i]);
	}
	delete stream;

	Common::String fileName = Common::String::format(
		"gamebot-dumps/%08x.wav", e->resId);
	Common::DumpFile out;
	if (!out.open(Common::Path(fileName), true)) {
		debugPrintf("Could not write %s\n", fileName.c_str());
		return true;
	}
	uint32 dataSize = samples.size() * 2;
	out.writeString("RIFF");
	out.writeUint32LE(36 + dataSize);
	out.writeString("WAVEfmt ");
	out.writeUint32LE(16);
	out.writeUint16LE(1);      // PCM
	out.writeUint16LE(1);      // mono
	out.writeUint32LE(22050);  // rate
	out.writeUint32LE(22050 * 2);
	out.writeUint16LE(2);
	out.writeUint16LE(16);
	out.writeString("data");
	out.writeUint32LE(dataSize);
	for (uint i = 0; i < samples.size(); i++)
		out.writeSint16LE(samples[i]);
	debugPrintf("Wrote %s (%u samples, %.2f s)\n", fileName.c_str(),
		samples.size(), samples.size() / 22050.0);
	return true;
}

// Simulates mouse input for scripted validation runs
bool Console::cmdClick(int argc, const char **argv) {
	if (!strcmp(argv[0], "rclick")) {
		g_engine->verbPalette().close();
		g_engine->inventoryUI().toggle();
		return true;
	}
	if (argc < 3) {
		debugPrintf("Usage: click <x> <y> (screen coordinates)\n");
		return true;
	}
	Common::Point pos((int16)atoi(argv[1]), (int16)atoi(argv[2]));
	g_engine->handleMouseMove(pos);
	g_engine->handleMouseClick(pos);
	return true;
}

bool Console::cmdUseWith(int argc, const char **argv) {
	if (argc < 3) {
		debugPrintf("Usage: usewith <targetObjectId> <inventoryObjectId>\n");
		return true;
	}
	g_engine->logic().interactWith(parseId(argv[1]), kVerbUse, parseId(argv[2]));
	return true;
}

bool Console::cmdDialog(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: dialog <dialogResId>\n");
		return true;
	}
	g_engine->logic().activateDialog(parseId(argv[1]));
	return true;
}

bool Console::cmdPick(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: pick <lineIndex>\n");
		return true;
	}
	g_engine->logic().pickSentence((uint)atoi(argv[1]));
	return true;
}

bool Console::cmdVerb(int argc, const char **argv) {
	if (argc < 3) {
		debugPrintf("Usage: verb <usar|coger|hablar|mirar|abrir|largarse> <objectId> [walk]\n");
		debugPrintf("With 'walk' the character walks to the object first.\n");
		return true;
	}
	static const struct { const char *name; Verb verb; } kVerbs[] = {
		{ "usar", kVerbUse }, { "coger", kVerbTake }, { "hablar", kVerbTalk },
		{ "mirar", kVerbLook }, { "abrir", kVerbOpen }, { "largarse", kVerbLeave },
	};
	for (const auto &v : kVerbs) {
		if (!strcmp(argv[1], v.name)) {
			uint32 objectId = parseId(argv[2]);
			if (argc > 3)
				g_engine->logic().interactWith(objectId, v.verb);
			else
				g_engine->logic().performVerb(objectId, v.verb);
			return true;
		}
	}
	debugPrintf("Unknown verb '%s'\n", argv[1]);
	return true;
}

bool Console::cmdInventory(int argc, const char **argv) {
	if (argc > 1) {
		g_engine->logic().addToInventory(parseId(argv[1]));
		return true;
	}
	uint n = 0;
	for (auto &entry : g_engine->logic().inventory()) {
		const ObjectEntry *object = g_engine->initialWorld().findObject(entry._key);
		debugPrintf("%08x %s\n", entry._key, object ? object->name.c_str() : "?");
		n++;
	}
	debugPrintf("%u objects (inv <objectId> adds one)\n", n);
	return true;
}

bool Console::cmdSay(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: say <textCode>\n");
		return true;
	}
	g_engine->logic().writer().showTextCode(parseId(argv[1]));
	return true;
}

bool Console::cmdWalk(int argc, const char **argv) {
	if (argc < 3) {
		debugPrintf("Usage: walk <x> <y>\n");
		return true;
	}
	Common::Point target((int16)atoi(argv[1]), (int16)atoi(argv[2]));
	if (g_engine->master().walkTo(g_engine->world(), target))
		debugPrintf("Walking to (%d,%d), %u steps\n", target.x, target.y,
			g_engine->master().path().size());
	else
		debugPrintf("No path to (%d,%d)\n", target.x, target.y);
	return true;
}

bool Console::cmdTeleport(int argc, const char **argv) {
	if (argc < 3) {
		debugPrintf("Usage: tp <x> <y> [layer]\n");
		return true;
	}
	Character &character = g_engine->master();
	World &world = g_engine->world();
	int16 x = (int16)atoi(argv[1]), y = (int16)atoi(argv[2]);
	uint16 layer = (argc > 3) ? (uint16)atoi(argv[3])
		: (world.hasWalkMap() ? (uint16)(world.mapHeight() + 2 - world.walkRowAt(Common::Point(x, y))) : character.layer());
	CharacterLocation location;
	location.characterId = character.objectId();
	location.x = x;
	location.y = y;
	location.orientation = character.orientation();
	location.layer = layer;
	character.enterPhase(world, location);
	character.visible = true;
	debugPrintf("Teleported to (%d,%d) L%u\n", x, y, layer);
	return true;
}

bool Console::cmdCharInfo(int argc, const char **argv) {
	Character &c = g_engine->master();
	debugPrintf("Character %08x: pos (%d,%d) L%u or%u scale %u%% %s%s\n",
		c.objectId(), c.x(), c.y(), c.layer(), c.orientation(), c.scale() / 10,
		c.isWalking() ? "walking" : "idle", c.visible ? "" : " (hidden)");
	if (c.isWalking())
		debugPrintf("Path: step %u of %u\n", c.pathPosition(), c.path().size());
	return true;
}

// Advances world time for scripted validation runs (gamebot_exec)
bool Console::cmdWait(int argc, const char **argv) {
	uint32 duration = (argc > 1) ? strtoul(argv[1], nullptr, 0) : 1000;
	uint32 end = g_system->getMillis() + duration;
	while (g_system->getMillis() < end && !g_engine->shouldQuit()) {
		uint32 millis = g_system->getMillis();
		g_engine->world().update(millis);
		g_engine->master().tick(millis, g_engine->world());
		if (g_engine->secondCharacter())
			const_cast<Character *>(g_engine->secondCharacter())->tick(millis, g_engine->world());
		g_engine->logic().update(millis);
		g_engine->world().draw(g_engine->_screen, &g_engine->master(), g_engine->secondCharacter());
		g_engine->drawHoverName(g_engine->_screen);
		g_engine->logic().writer().draw(g_engine->_screen);
		g_engine->logic().drawDialog(g_engine->_screen);
		g_engine->changerBadge().update(millis);
		g_engine->changerBadge().draw(g_engine->_screen);
		g_engine->verbPalette().draw(g_engine->_screen);
		g_engine->inventoryUI().draw(g_engine->_screen);
		g_engine->mainMenu().draw(g_engine->_screen);
		g_engine->_screen->update();
		g_system->delayMillis(10);
	}
	return true;
}

bool Console::cmdOverlay(int argc, const char **argv) {
	World &world = g_engine->world();
	bool &flag = !strcmp(argv[0], "showwalkmap") ? world._showWalkMap
		: !strcmp(argv[0], "showpath") ? world._showPath : world._showHotspots;
	flag = (argc > 1) ? atoi(argv[1]) != 0 : !flag;
	debugPrintf("%s = %d\n", argv[0], flag);
	return true;
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

bool Console::cmdGoto(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: goto <phaseId>  (e.g. goto 0x102)\n");
		return true;
	}
	uint32 phaseId = parseId(argv[1]);
	if (g_engine->gotoPhase(phaseId))
		debugPrintf("Now in phase %08x (%dx%d)\n", phaseId,
			g_engine->world().phaseWidth(), g_engine->world().phaseHeight());
	else
		debugPrintf("Could not load phase %08x\n", phaseId);
	return true;
}

bool Console::cmdScroll(int argc, const char **argv) {
	World &world = g_engine->world();
	if (argc < 3) {
		debugPrintf("Scroll origin: (%d, %d); usage: scroll <x> <y>\n",
			world.origin().x, world.origin().y);
		return true;
	}
	world.origin().x = (int16)CLIP<int32>(atoi(argv[1]), 0, MAX(0, world.phaseWidth() - kScreenWidth));
	world.origin().y = (int16)CLIP<int32>(atoi(argv[2]), 0, MAX(0, world.phaseHeight() - kScreenHeight));
	debugPrintf("Scroll origin set to (%d, %d)\n", world.origin().x, world.origin().y);
	return true;
}

bool Console::cmdScreenshot(int argc, const char **argv) {
	Common::String fileName = (argc > 1) ? argv[1] : "gamebot-dumps/screenshot.bmp";

	// Render a fresh frame and save it with the current system palette
	g_engine->world().draw(g_engine->_screen, &g_engine->master(), g_engine->secondCharacter());
	g_engine->drawHoverName(g_engine->_screen);
	g_engine->logic().writer().draw(g_engine->_screen);
	g_engine->logic().drawDialog(g_engine->_screen);
	g_engine->changerBadge().draw(g_engine->_screen);
	g_engine->verbPalette().draw(g_engine->_screen);
	g_engine->inventoryUI().draw(g_engine->_screen);
	g_engine->mainMenu().draw(g_engine->_screen);
	byte palette[256 * 3];
	g_system->getPaletteManager()->grabPalette(palette, 0, 256);

	Common::DumpFile out;
	if (out.open(Common::Path(fileName), true) &&
			Image::writeBMP(out, g_engine->_screen->rawSurface(), palette, 256))
		debugPrintf("Wrote %s\n", fileName.c_str());
	else
		debugPrintf("Could not write %s\n", fileName.c_str());
	return true;
}

} // End of namespace Gamebot
