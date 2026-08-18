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
#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "graphics/fontman.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/paletteman.h"
#include "graphics/screen.h"

#include "gamebot/character.h"
#include "gamebot/debug-names.h"
#include "gamebot/gamebot.h"
#include "gamebot/logic.h"
#include "gamebot/world.h"

namespace Gamebot {

// Guard against runaway rule chains
static int s_dispatchDepth = 0;

// Text colors of the original WriterMaster, mapped onto the current
// palette the same way GDI matched them on the 8bpp screen
static byte findNearestColor(byte r, byte g, byte b) {
	byte palette[256 * 3];
	g_system->getPaletteManager()->grabPalette(palette, 0, 256);
	uint best = 255;
	uint32 bestDist = 0xffffffff;
	for (uint i = 0; i < 256; i++) {
		int dr = (int)palette[i * 3] - r;
		int dg = (int)palette[i * 3 + 1] - g;
		int db = (int)palette[i * 3 + 2] - b;
		uint32 dist = (uint32)(dr * dr + dg * dg + db * db);
		if (dist < bestDist) {
			bestDist = dist;
			best = i;
		}
	}
	return (byte)best;
}

// The original creates a 24-pixel Comic Sans MS for the screen texts
// and a 19-pixel bold one for the dialog sentences. A comic.ttf (and
// comicbd.ttf) next to the game data is used when present, then the
// bundled Liberation fonts, then the GUI font as a last resort.
static const Graphics::Font *loadWriterFont(bool dialog) {
	const Graphics::Font *font = nullptr;
#ifdef USE_FREETYPE2
	// Original Comic Sans sizes are 24/19; the stand-in Liberation
	// runs wider, so it drops a step to keep the same line lengths
	Common::File *file = new Common::File();
	int size = dialog ? 19 : 24;
	if (!file->exists(dialog ? "comicbd.ttf" : "comic.ttf"))
		size = 20;
	if (file->open(dialog ? "comicbd.ttf" : "comic.ttf")) {
		font = Graphics::loadTTFFont(file, DisposeAfterUse::YES, size,
			Graphics::kTTFSizeModeCell);
		if (font)
			return font;
	} else {
		delete file;
	}
	font = Graphics::loadTTFFontFromArchive(
		dialog ? "LiberationSans-Bold.ttf" : "LiberationSans-Regular.ttf",
		size, Graphics::kTTFSizeModeCell);
	if (font)
		return font;
#endif
	return FontMan.getFontByUsage(dialog ?
		Graphics::FontManager::kGUIFont : Graphics::FontManager::kBigGUIFont);
}

const Graphics::Font *TextWriter::screenFont() {
	static const Graphics::Font *font = nullptr;
	if (!font)
		font = loadWriterFont(false);
	return font;
}

const Graphics::Font *TextWriter::dialogFont() {
	static const Graphics::Font *font = nullptr;
	if (!font)
		font = loadWriterFont(true);
	return font;
}

void TextWriter::showTextCode(uint32 textCode) {
	const ResourceEntry *e = g_engine->resources().findByResId(textCode);
	if (!e || e->type != kResText) {
		warning("Text code %08x not found", textCode);
		return;
	}
	byte *data = g_engine->resources().readBlob(*e);
	if (!data)
		return;
	uint32 textLen = 0;
	while (textLen < e->size && data[textLen] != '\0')
		textLen++;
	showString(Common::String((const char *)data, textLen));
	delete[] data;
}

void TextWriter::showString(const Common::String &text) {
	_text = text;
	// Display time scales with the phrase length, similar to the
	// original voice-driven timing
	_hideTime = g_system->getMillis() + 1200 + 60 * text.size();
	debugC(kDebugEvents, "Writer: \"%s\"", text.c_str());
}

void TextWriter::update(uint32 millis) {
	if (!_text.empty() && millis >= _hideTime)
		_text.clear();
}

void TextWriter::draw(Graphics::Screen *screen) const {
	if (!_text.empty())
		drawText(screen, _text, false);
}

// Original writer layout: a strip at the bottom of the screen, 30
// pixels per line and at most two lines, each centered and clipped;
// the text is yellow (hot orange when highlighted) over a shadow
// printed twice at growing offsets
void TextWriter::drawText(Graphics::Screen *screen, const Common::String &text,
		bool highlight) const {
	if (text.empty())
		return;
	const Graphics::Font *font = screenFont();
	if (!font)
		return;

	const int kLineHeight = 30; // WrNormalHeight
	Common::Array<Common::U32String> lines;
	font->wordWrapText(Common::U32String(text, Common::kISO8859_1), screen->w, lines);
	if (lines.size() > 2) {
		// The original cuts what does not fit, adding an ellipsis
		lines.resize(2);
		lines[1] += Common::U32String("...");
	}

	byte shade = findNearestColor(5, 5, 5);
	byte color = highlight ? findNearestColor(255, 120, 0)
		: findNearestColor(255, 255, 5);
	int top = screen->h - kLineHeight * (int)lines.size();
	for (uint i = 0; i < lines.size(); i++) {
		int y = top + (int)i * kLineHeight + 1;
		int x = (screen->w - font->getStringWidth(lines[i])) / 2;
		font->drawString(screen, lines[i], x + 1, y + 1, screen->w, shade);
		font->drawString(screen, lines[i], x + 2, y + 2, screen->w, shade);
		font->drawString(screen, lines[i], x, y, screen->w, color);
	}
}

bool Logic::isBusy() const {
	return _writer.active() || _scriptedWalk != 0 || _pendingAnswerAnim != 0 ||
		_pendingTake != 0 || g_engine->master().isActionAnimating() ||
		g_engine->mortadelo().sceneAnimActive() ||
		g_engine->filemon().sceneAnimActive();
}

void Logic::addToInventory(uint32 objectId) {
	if (!_inventory.contains(objectId))
		_inventoryOrder.push_back(objectId);
	_inventory[objectId] = true;
	setObjectEnabled(objectId, false);
	debugC(kDebugActions, "Object %08x added to the inventory", objectId);
}

// A disabled object leaves the inventory too, as the original
// InventMaster extracts it on the disable event
void Logic::removeFromInventory(uint32 objectId) {
	if (!_inventory.contains(objectId))
		return;
	_inventory.erase(objectId);
	for (uint i = 0; i < _inventoryOrder.size(); i++) {
		if (_inventoryOrder[i] == objectId) {
			_inventoryOrder.remove_at(i);
			break;
		}
	}
	if (g_engine->linkedObject() == objectId)
		g_engine->linkObject(0);
	debugC(kDebugActions, "Object %08x removed from the inventory", objectId);
}

bool Logic::isObjectEnabled(uint32 objectId) const {
	if (isInInventory(objectId))
		return true;
	if (g_engine->world().hasObject(objectId))
		return g_engine->world().isEnabled(objectId);
	if (_objectEnabled.contains(objectId))
		return _objectEnabled.getVal(objectId);
	if (g_engine->initialWorld().isLayer0Object(objectId))
		return false;
	return true;
}

void Logic::setObjectEnabled(uint32 objectId, bool enabled) {
	if (enabled) {
		// An item that is in the player's inventory or already placed elsewhere cannot be re-enabled in the scene
		if (isInInventory(objectId))
			return;
		if (objectId == 0x05140006 && isObjectEnabled(0x05160005)) // C05P14Tuberia2 placed as C05P16Tuberia
			return;
	}
	_objectEnabled[objectId] = enabled;
	g_engine->world().setEnabled(objectId, enabled);
}

void Logic::applyObjectStates() {
	for (auto &entry : _objectEnabled)
		g_engine->world().setEnabled(entry._key, entry._value);
}

void Logic::syncGame(Common::Serializer &s) {
	// Inventory
	uint32 count = _inventoryOrder.size();
	s.syncAsUint32LE(count);
	if (s.isLoading()) {
		_inventory.clear();
		_inventoryOrder.clear();
		for (uint32 i = 0; i < count; i++) {
			uint32 id = 0;
			s.syncAsUint32LE(id);
			_inventory[id] = true;
			_inventoryOrder.push_back(id);
		}
	} else {
		for (uint32 i = 0; i < count; i++)
			s.syncAsUint32LE(_inventoryOrder[i]);
	}

	// Object enabled overrides
	count = _objectEnabled.size();
	s.syncAsUint32LE(count);
	if (s.isLoading()) {
		_objectEnabled.clear();
		for (uint32 i = 0; i < count; i++) {
			uint32 id = 0;
			byte enabled = 0;
			s.syncAsUint32LE(id);
			s.syncAsByte(enabled);
			_objectEnabled[id] = enabled != 0;
		}
	} else {
		for (auto &entry : _objectEnabled) {
			uint32 id = entry._key;
			byte enabled = entry._value ? 1 : 0;
			s.syncAsUint32LE(id);
			s.syncAsByte(enabled);
		}
	}

	// Dialog sentence states (only dialogs that have been touched)
	count = _dialogs.size();
	s.syncAsUint32LE(count);
	if (s.isLoading()) {
		_dialogs.clear();
		for (uint32 i = 0; i < count; i++) {
			uint32 dialogId = 0;
			s.syncAsUint32LE(dialogId);
			loadDialog(dialogId);
			uint32 sentenceCount = 0;
			s.syncAsUint32LE(sentenceCount);
			for (uint32 j = 0; j < sentenceCount; j++) {
				uint32 flags = 0;
				s.syncAsUint32LE(flags);
				if (_dialogs.contains(dialogId) && j < _dialogs[dialogId].size())
					_dialogs[dialogId][j].flags = flags;
			}
		}
	} else {
		for (auto &entry : _dialogs) {
			uint32 dialogId = entry._key;
			s.syncAsUint32LE(dialogId);
			uint32 sentenceCount = entry._value.size();
			s.syncAsUint32LE(sentenceCount);
			for (uint32 j = 0; j < sentenceCount; j++)
				s.syncAsUint32LE(entry._value[j].flags);
		}
	}
}

static uint32 verbEvent(Verb verb) {
	switch (verb) {
	case kVerbTake: return kEventObjTakeNow;
	case kVerbTalk: return kEventObjTalkNow;
	case kVerbLook: return kEventObjLookNow;
	case kVerbOpen: return kEventObjOpenNow;
	case kVerbLeave: return kEventObjLeaveNow;
	case kVerbUseInventory: return kEventObjUsarInvent;
	default: return kEventObjUseNow;
	}
}

void Logic::interactWith(uint32 objectId, Verb verb, uint32 linkedObjectId) {
	const ObjectEntry *object = g_engine->initialWorld().findObject(objectId);
	if (!object)
		return;

	_pendingObject = objectId;
	_pendingLinked = linkedObjectId;
	_pendingVerb = verb;
	_pendingActive = true;

	// Walk to the object's interaction point; the verb runs on arrival
	Character &character = g_engine->master();
	if (object->targetX || object->targetY)
		character.walkTo(g_engine->world(), Common::Point(object->targetX, object->targetY));
	debugC(kDebugActions, "Walking to %08x '%s' for verb %d",
		objectId, object->name.c_str(), (int)verb);
}

void Logic::performVerb(uint32 objectId, Verb verb, uint32 linkedObjectId) {
	uint32 eventId = verbEvent(verb);
	uint matched = dispatchEvent(eventId, objectId, linkedObjectId);
	if (matched)
		return;

	// Taking a takeable object without a custom rule plays the generic gesture first;
	// the pickup fires at HALF the animation, as the original posts the CogerYa from
	// the gesture's middle frame
	if (verb == kVerbTake) {
		const ObjectEntry *object = g_engine->initialWorld().findObject(objectId);
		if (object && (object->flags & ObjectEntry::kFlagTakeable)) {
			uint32 animCode = kResTakeCrouch;
			if (object->flags & ObjectEntry::kFlagTakeFront)
				animCode = kResTakeFront;
			if (object->flags & ObjectEntry::kFlagTakeAbove)
				animCode = kResTakeAbove;
			if (!(object->flags & ObjectEntry::kFlagTakeDirect) &&
					g_engine->master().playActionAnim(animCode)) {
				_pendingTake = objectId;
				return;
			}
			addToInventory(objectId);
			return;
		}
	}

	// No rule handled the verb: the original answers with a
	// generic "I can't do that" phrase chosen by the TCAU flags
	const ObjectEntry *object = g_engine->initialWorld().findObject(objectId);
	debugC(kDebugActions, "No rule for %s on %08x, generic response",
		eventName(eventId) ? eventName(eventId) : "?", objectId);
	sayGenericResponse(eventId, object ? object->impossibleResponses : 0);
}

// Generic response phrase table of the original Character.cpp: per
// category up to nine phrase codes picked at random. The category
// depends on the verb, whether the object is a person (kTypePerson)
// and whether "maybe another way" applies (kTake/kOpen/kUse flags).
void Logic::sayGenericResponse(uint32 verbEventId, uint16 responseFlags) {
	enum { kTypePerson = 0x01, kFlagTake = 0x02, kFlagOpen = 0x04, kFlagUse = 0x08 };
	enum { kCantOpen = 0x01, kCantPick = 0x03, kCantTalk = 0x05, kCantLook = 0x07,
		kCantUse = 0x09, kMaybe = 0x0a };

	static const uint16 kResponses[][9] = {
		{ 0x9111, 0x9112, 0x9113, 0x9114, 0x9115, 0x9116, 0x9117, 0x9118, 0 },
		{ 0x9121, 0x9122, 0x9123, 0x9124, 0x9125, 0x9126, 0x9127, 0x9128, 0x9129 },
		{ 0x9131, 0x9132, 0x9133, 0x9134, 0x9135, 0x9136, 0x9137, 0, 0 },
		{ 0x9141, 0x9142, 0x9143, 0x9144, 0x9145, 0x9146, 0x9147, 0x9148, 0x9149 },
		{ 0x9151, 0x9152, 0x9153, 0x9154, 0x9155, 0x9156, 0, 0, 0 },
		{ 0x9161, 0x9162, 0x9163, 0x9164, 0x9165, 0x9166, 0, 0, 0 },
		{ 0x9171, 0x9172, 0, 0, 0, 0, 0, 0, 0 },
		{ 0x9181, 0x9182, 0x9183, 0x9184, 0, 0, 0, 0, 0 },
		{ 0x91a1, 0x91a2, 0x91a3, 0x91a4, 0x91a5, 0x91a6, 0x91a7, 0, 0 },
		{ 0x91b1, 0x91b2, 0x91b3, 0x91b4, 0x91b5, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0x91c1, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0x91d1, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0x91e1, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0x91f1, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0x91f2, 0x91f3, 0, 0, 0, 0, 0, 0, 0 },
	};

	int index = 0;
	switch (verbEventId) {
	case kEventObjLookNow:
		index = kCantLook;
		break;
	case kEventObjOpenNow:
		index = kCantOpen + ((responseFlags & kFlagOpen) ? kMaybe : 0);
		break;
	case kEventObjTalkNow:
		index = kCantTalk;
		break;
	case kEventObjTakeNow:
		index = kCantPick + ((responseFlags & kFlagTake) ? kMaybe : 0);
		break;
	case kEventObjUseNow:
	case kEventObjUsarInvent:
		index = kCantUse + ((responseFlags & kFlagUse) ? kMaybe : 0);
		break;
	default:
		return;
	}
	if (index && (responseFlags & kTypePerson))
		index--;
	if (!kResponses[index][0])
		index = 0;

	uint16 code = 0;
	while (!code)
		code = kResponses[index][g_engine->getRandomNumber(8)];

	// Phrase codes belong to the master character's own resources
	uint32 textCode = ((g_engine->master().objectId() << 8) & 0xffff0000) | code;
	sayPhrase(textCode, textCode - 0x100);
}

void Logic::sayPhrase(uint32 textCode, uint32 soundCode) {
	_writer.showTextCode(textCode);
	if (soundCode) {
		_phraseSound = soundCode;
		g_engine->sounds().playSound(soundCode);
	}
	// Speaking stops a walk outright, as the original kills every
	// timer of the character when a phrase starts
	g_engine->master().stopWalking();
	g_engine->master().setTalking(true);
}

void Logic::update(uint32 millis) {
	// Finished sounds fire the original pop-ended event, which some
	// rules wait on (e.g. the snow mound disappears when Momiez's
	// greeting voice ends)
	Common::Array<uint32> finishedSounds;
	g_engine->sounds().pollFinishedSounds(finishedSounds);
	for (uint i = 0; i < finishedSounds.size(); i++) {
		// A spoken phrase ends the moment its voice does, as the
		// original clears on the pop-ended of the waited sound
		if (finishedSounds[i] == _phraseSound && _writer.active())
			_writer.expire();
		dispatchEvent(kEventSoundPopEnded, finishedSounds[i], 0);
	}

	// A phrase with a voice stays on screen while its own voice plays
	if (_phraseSound && g_engine->sounds().isSoundPlaying(_phraseSound))
		_writer.keepAlive(millis);

	bool phraseWasActive = _writer.active();
	_writer.update(millis);
	if (phraseWasActive && !_writer.active())
		onPhraseEnded();

	// Fire the pending verb when the character arrives, first taking
	// the object's interaction pose (orientation, layer and position
	// snap of the original arrival handling)
	if (_pendingActive && !g_engine->master().isWalking()) {
		_pendingActive = false;
		const ObjectEntry *object = g_engine->initialWorld().findObject(_pendingObject);
		if (object && (object->targetX || object->targetY))
			g_engine->master().takeInteractionPose(g_engine->world(), *object);
		performVerb(_pendingObject, _pendingVerb, _pendingLinked);
	}

	// Safety net: a gesture that ends without reaching its middle
	// frame still completes the take
	if (_pendingTake && !g_engine->master().isActionAnimating())
		onTakeGestureHalf();

	// Announce the end of a script-driven walk; the character takes
	// the orientation the walk request carried
	if (_scriptedWalk && !g_engine->master().isWalking()) {
		uint32 packed = _scriptedWalk;
		_scriptedWalk = 0;
		if (_scriptedWalkOrient)
			g_engine->master().setOrientation((uint16)_scriptedWalkOrient);
		dispatchEvent(kEventObjArrived, packed, 0);
	}
}

uint Logic::dispatchDirectEvent(uint32 eventId, uint32 param1, uint32 param2, uint32 lParam) {
	return dispatchEvent(eventId, param1, param2, lParam);
}

uint Logic::dispatchEvent(uint32 eventId, uint32 param1, uint32 param2, uint32 lParam) {
	if (s_dispatchDepth > 8) {
		warning("Action rule chain too deep, stopping at %s", eventName(eventId));
		return 0;
	}
	s_dispatchDepth++;

	// Every object's rule table sees every event, as in the original
	// Snapshot the active objects and matching rules for this event
	// before running actions, so that state changes made by one object
	// (e.g. enabling Seiscientos2 and disabling Seiscientos1) do not
	// cause subsequent objects to falsely trigger on the SAME event.
	// Furthermore, in the original VisualPhase::Dispatch, once an object
	// handles an evObj* event, processing stops (DoCheck returns 1).
	ActionFile &actions = g_engine->actions();
	struct Match {
		uint ruleIndex;
		uint32 owner;
	};
	Common::Array<Match> matchedRules;
	uint32 handledOwner = 0;

	for (uint i = 0; i < actions.ruleCount(); i++) {
		const ActionRule &rule = actions.rule(i);
		if (rule.eventId != eventId || rule.eventParam1 != param1)
			continue;
		// A rule that names a second parameter (e.g. the inventory
		// object of a use-with) only fires on an exact match
		if (rule.eventParam2 && rule.eventParam2 != param2)
			continue;

		uint32 owner = actions.ruleOwner(i);
		if (owner >= 0x100 && !isObjectEnabled(owner))
			continue;

		if (handledOwner && handledOwner != owner)
			continue;

		if (!checkConditions(rule, owner, lParam))
			continue;

		Match m;
		m.ruleIndex = i;
		m.owner = owner;
		matchedRules.push_back(m);
		handledOwner = owner;
	}

	for (uint i = 0; i < matchedRules.size(); i++) {
		const ActionRule &rule = actions.rule(matchedRules[i].ruleIndex);
		uint32 owner = matchedRules[i].owner;
		debugC(kDebugActions, "Rule: on %s(%x,%x) [owner=%08x] do %s(%x,%x,%x)",
			eventName(eventId) ? eventName(eventId) : "?", param1, param2, owner,
			actionName(rule.actionId) ? actionName(rule.actionId) : "?",
			rule.actionParam1, rule.actionParam2, rule.actionParam3);
		runAction(rule, owner);
	}
	s_dispatchDepth--;
	return matchedRules.size();
}

bool Logic::checkConditions(const ActionRule &rule, uint32 owner, uint32 lParam) {
	// All conditions must hold; the OR flag exists in the original
	// headers but its dispatcher never evaluates it, so plain AND is
	// the faithful behavior. A zero argument means the rule's owner.
	for (uint c = 0; c < 6; c++) {
		uint16 condition = rule.conditions[c];
		if (!condition)
			continue;
		bool result = true;
		uint32 arg = rule.condArgs[c] ? rule.condArgs[c] : owner;
		switch (condition & 0xff) {
		case kIfEnabled:
			result = isObjectEnabled(arg);
			break;
		case kIfInInventory:
			result = isInInventory(arg);
			break;
		case kIfInBounds:
			result = g_engine->world().hitTestObject(arg, g_engine->lastMousePhasePos());
			break;
		case kIfLParam:
			result = (rule.condArgs[c] == lParam);
			break;
		default:
			break;
		}
		if (condition & kConditionNot)
			result = !result;
		if (!result)
			return false;
	}
	return true;
}

void Logic::resetGame() {
	_inventory.clear();
	_objectEnabled.clear();
	_dialogs.clear();
	_writer.update(UINT32_MAX); // clears any phrase
	_phraseSound = 0;
	_pendingActive = false;
	_scriptedWalk = 0;
	endDialog();
	// A new game runs the chapter 1 chain: an animation phase, the
	// chapter video and then the street with its scripted intro
	g_engine->gotoPhase(0x2001);
}

// Starts an event animation from a rule or message. Animations owned
// by a character (e.g. Mortadelo picking the door lock) draw at an
// absolute scene position, so they run as detached front items with
// the character hidden until they end.
void Logic::startEventAnim(const ResourceEntry &e) {
	Character *ch = g_engine->characterById(e.objectId);
	if (ch)
		ch->startSceneAnim(e);
	else
		g_engine->world().startAnimation(e.objectId, e.resId);
}

void Logic::runAction(const ActionRule &rule, uint32 owner) {
	if ((rule.actionId & 0xFF000000) != 0 || rule.actionId >= 0x100) {
		handleMessage(rule.actionId, rule.actionParam1, rule.actionParam2, owner);
		return;
	}

	switch (rule.actionId) {
	case kActionSendMsg:
		handleMessage(rule.actionParam1, rule.actionParam2, rule.actionParam3, owner);
		break;
	case kActionStartAnimation: {
		const ResourceEntry *e = g_engine->resources().findByResId(rule.actionParam1);
		if (!e) {
			warning("Animation %08x not found", rule.actionParam1);
		} else if (e->type == kResAnimationFlic) {
			g_engine->playVideo(rule.actionParam1);
			onAnimationEnded(rule.actionParam1);
		} else {
			startEventAnim(*e);
		}
		break;
	}
	case kActionEnable:
		// The original direct enable/disable actions act on the rule
		// owner; targeting another object goes through send-message
		setObjectEnabled(owner, true);
		break;
	case kActionDisable:
		setObjectEnabled(owner, false);
		break;
	case kActionPhraseOn:
		// The phrase's owner freezes its ambient animations while
		// the line plays, as the original waits on running autos
		if (owner && owner != g_engine->master().objectId()) {
			_phraseOwner = owner;
			g_engine->world().pauseObjectAnims(owner, true);
		}
		sayPhrase(rule.actionParam1, rule.actionParam2);
		break;
	case kActionInputEnable:
		debugC(kDebugActions, "Input enable %x (TODO)", rule.actionParam1);
		break;
	default:
		warning("Unknown action %x", rule.actionId);
		break;
	}
}

void Logic::handleMessage(uint32 eventCode, uint32 param2, uint32 param3, uint32 owner) {
	// State side effects of the well-known messages, then the event
	// is offered to the rule tables so chains keep running
	switch (eventCode) {
	case kEventObjToInventory:
		addToInventory(param2);
		break;
	case kEventObjDisable:
		setObjectEnabled(param2, false);
		removeFromInventory(param2);
		break;
	case kEventObjEnable:
		setObjectEnabled(param2, true);
		break;
	case kEventObjActivateAnim: {
		const ResourceEntry *e = g_engine->resources().findByResId(param2);
		if (e && e->type == kResAnimationFlic) {
			g_engine->playVideo(param2);
			onAnimationEnded(param2);
		} else if (e) {
			startEventAnim(*e);
		}
		break;
	}
	case kEventDialogActivate:
		activateDialog(param2, owner);
		break;
	case kEventAppPhaseChange:
		// Phase 0x99 in the original engine is the signal to open the main menu
		if (param2 == 0x99) {
			g_engine->mainMenu().open();
			return;
		}
		if (!g_engine->gotoPhase(param2)) {
			debugC(kDebugActions, "Phase %08x is the menu panel", param2);
			g_engine->mainMenu().open();
		}
		return; // gotoPhase already ran any follow-up chain
	case kEventOptionsActivate:
		g_engine->mainMenu().open();
		return;
	case kEventPersSetMaster:
		// The scripted conversations switch the speaking character
		g_engine->setMasterById(param2);
		break;
	case kEventPersWalkTo: {
		// Script-driven walk to a packed (y << 16 | x) point; the
		// arrival is announced with the same packed parameter
		Common::Point target((int16)(param2 & 0xffff), (int16)(param2 >> 16));
		debugC(kDebugActions, "Scripted walk to (%d,%d) orient %u", target.x, target.y, param3);
		if (g_engine->master().walkTo(g_engine->world(), target)) {
			_scriptedWalk = param2;
			_scriptedWalkOrient = param3;
		} else {
			if (param3)
				g_engine->master().setOrientation((uint16)param3);
			dispatchEvent(kEventObjArrived, param2, 0);
		}
		break;
	}
	case kEventTextClean:
		// The soft clean does not erase spoken phrases (they display
		// in the sticky mode of the original writer); only the full
		// clean (0x0A000040) wipes unconditionally
		break;
	case 0x0A000040: // evTextFullClean
		_writer.update(UINT32_MAX);
		break;
	case kEventFXStartEffect:
		g_engine->world().setWeather(param2);
		break;
	case kEventSoundPlayPop:
		g_engine->sounds().playSound(param2, Audio::Mixer::kSFXSoundType, owner);
		break;
	case kEventTextWriteCode:
		_writer.showTextCode(param2);
		break;
	case kEventTextWriteStr: {
		const ResourceEntry *e = g_engine->resources().findByResId(param2);
		if (e && e->type == kResText)
			_writer.showTextCode(param2);
		break;
	}
	case kEventDialogSetFrase: { // evDialogSetFrase: toggle a sentence by text id
		bool found = false;
		for (auto &dialog : _dialogs) {
			for (uint i = 0; i < dialog._value.size(); i++) {
				if (dialog._value[i].textId == param2) {
					if (param3)
						dialog._value[i].flags |= kDialogActive;
					else
						dialog._value[i].flags &= ~kDialogActive;
					found = true;
					break;
				}
			}
			if (found) break;
		}

		if (!found) {
			// Dialog not loaded yet; load all unloaded dialogs until we find it.
			ResourceFile *sources[] = { &g_engine->dialogFile(), &g_engine->resources() };
			for (int s = 0; s < 2 && !found; s++) {
				for (uint k = 0; k < sources[s]->count(); k++) {
					const ResourceEntry &e = sources[s]->entry(k);
					if (e.type == kResDialog && !_dialogs.contains(e.resId)) {
						loadDialog(e.resId);
						for (uint i = 0; i < _dialogs[e.resId].size(); i++) {
							if (_dialogs[e.resId][i].textId == param2) {
								if (param3)
									_dialogs[e.resId][i].flags |= kDialogActive;
								else
									_dialogs[e.resId][i].flags &= ~kDialogActive;
								found = true;
								break;
							}
						}
						if (found) break;
					}
				}
			}
			if (!found)
				debugC(2, kDebugActions, "evDialogSetFrase: sentence %08x not found in any dialog", param2);
		}
		break;
	}
	default:
		break;
	}
	dispatchEvent(eventCode, param2, param3);
}

bool Logic::skipPhrase() {
	if (!_writer.active())
		return false;
	if (_phraseSound)
		g_engine->sounds().stopSound(_phraseSound);
	_writer.update(UINT32_MAX);
	debugC(kDebugEvents, "Phrase skipped");
	onPhraseEnded();
	return true;
}

// A click while a scripted scene runs fast-forwards it one piece at
// a time: first the spoken line, then any running event animation
bool Logic::skipCutscene() {
	if (skipPhrase())
		return true;
	if (g_engine->mortadelo().sceneAnimActive()) {
		g_engine->mortadelo().finishSceneAnim();
		return true;
	}
	if (g_engine->filemon().sceneAnimActive()) {
		g_engine->filemon().finishSceneAnim();
		return true;
	}
	if (g_engine->world().skipEventAnimation()) {
		return true;
	}
	return false;
}

void Logic::onPhraseEnded() {
	g_engine->master().setTalking(false);
	if (_phraseOwner) {
		g_engine->world().pauseObjectAnims(_phraseOwner, false);
		_phraseOwner = 0;
	}

	// The original signals the end of a spoken phrase with an
	// AnimEnded event carrying the voice code, which rules use to
	// chain reactions
	if (_phraseSound) {
		uint32 sound = _phraseSound;
		_phraseSound = 0;
		dispatchEvent(kEventObjAnimEnded, sound, 0);
	}

	// Inside a conversation the spoken line either runs the NPC's
	// answer animation or goes straight back to the sentence list
	if (_currentDialog && !_dialogOpen && !_pendingAnswerAnim) {
		if (_answerAnim) {
			uint32 anim = _answerAnim;
			_answerAnim = 0;
			const ResourceEntry *e = g_engine->resources().findByResId(anim);
			if (e && g_engine->world().startAnimation(e->objectId, anim)) {
				_pendingAnswerAnim = anim;
				return;
			}
			debugC(kDebugActions, "Answer animation %08x unavailable", anim);
		}
		if (_sentenceFlags & kDialogGoodbye)
			endDialog();
		else
			showDialogList();
	}
}

// Called from the take gesture's middle frame: the take event and
// the built-in pickup run there, halfway through the crouch
void Logic::onTakeGestureHalf() {
	if (!_pendingTake)
		return;
	uint32 object = _pendingTake;
	_pendingTake = 0;
	uint matched = dispatchEvent(kEventObjTakeNow, object, 0);
	if (!matched)
		addToInventory(object);
}

void Logic::onAnimationEnded(uint32 resId) {
	if (_pendingAnswerAnim && resId == _pendingAnswerAnim) {
		_pendingAnswerAnim = 0;
		if (_sentenceFlags & kDialogGoodbye)
			endDialog();
		else
			showDialogList();
		// The end still reaches the rule tables: chains like Momiez's
		// password hang exactly on the answer animation's end
	}

	dispatchEvent(kEventObjAnimEnded, resId, 0);
}

bool Logic::loadDialog(uint32 dialogId) {
	if (_dialogs.contains(dialogId))
		return true;

	// The state file (default.dlg) carries the initial activation
	// flags; the resource file has the pristine sentences
	ResourceFile *source = &g_engine->dialogFile();
	const ResourceEntry *e = source->count() ? source->findByResId(dialogId) : nullptr;
	if (!e) {
		source = &g_engine->resources();
		e = source->findByResId(dialogId);
	}
	if (!e || e->type != kResDialog) {
		warning("Dialog %08x not found", dialogId);
		return false;
	}

	byte *data = source->readBlob(*e);
	if (!data)
		return false;

	uint32 count = READ_LE_UINT32(data);
	Common::Array<DialogSentence> sentences;
	sentences.resize(count);
	for (uint32 i = 0; i < count; i++) {
		const byte *p = data + 4 + i * 16;
		sentences[i].textId = READ_LE_UINT32(p);
		sentences[i].soundId = READ_LE_UINT32(p + 4);
		sentences[i].animId = READ_LE_UINT32(p + 8);
		sentences[i].flags = READ_LE_UINT32(p + 12);
	}
	delete[] data;
	_dialogs[dialogId] = sentences;
	debugC(kDebugActions, "Dialog %08x loaded: %u sentences", dialogId, count);
	return true;
}

void Logic::activateDialog(uint32 dialogId, uint32 ownerId) {
	if (!loadDialog(dialogId))
		return;
	_currentDialog = dialogId;
	_answerAnim = 0;
	_pendingAnswerAnim = 0;
	_sentenceFlags = 0;
	// The talked-to object stops its ambient animations for the whole
	// conversation (original StopAutoAnim on the dialog activation)
	_dialogOwner = ownerId;
	if (_dialogOwner)
		g_engine->world().pauseObjectAnims(_dialogOwner, true);
	showDialogList();
}

void Logic::showDialogList() {
	_visibleSentences.clear();
	const Common::Array<DialogSentence> &sentences = _dialogs[_currentDialog];
	for (uint i = 0; i < sentences.size(); i++) {
		if (sentences[i].flags & kDialogActive)
			_visibleSentences.push_back(i);
	}
	if (_visibleSentences.empty()) {
		endDialog();
		return;
	}
	// The original shuffles the sentence list on every opening (its
	// ReorderBag sorts the entries by a fresh rand() key)
	for (uint i = _visibleSentences.size() - 1; i > 0; i--) {
		uint j = g_engine->getRandomNumber(i);
		SWAP(_visibleSentences[i], _visibleSentences[j]);
	}
	_dialogOpen = true;
	debugC(kDebugActions, "Dialog %08x offers %u sentences",
		_currentDialog, _visibleSentences.size());
}

void Logic::endDialog() {
	uint32 dialogId = _currentDialog;
	_currentDialog = 0;
	_dialogOpen = false;
	_visibleSentences.clear();
	if (_dialogOwner) {
		g_engine->world().pauseObjectAnims(_dialogOwner, false);
		_dialogOwner = 0;
	}
	if (dialogId) {
		debugC(kDebugActions, "Dialog %08x ended", dialogId);
		dispatchEvent(0x09000004 /* evDialogEnded */, dialogId, 0);
	}
}

void Logic::pickSentence(uint index) {
	if (!_dialogOpen || index >= _visibleSentences.size())
		return;
	DialogSentence &sentence = _dialogs[_currentDialog][_visibleSentences[index]];
	_dialogOpen = false;
	_visibleSentences.clear();

	if (sentence.flags & kDialogDeactivate)
		sentence.flags &= ~kDialogActive;
	_sentenceFlags = sentence.flags;
	_answerAnim = (sentence.flags & kDialogAnswer) ? sentence.animId : 0;

	debugC(kDebugActions, "Dialog line %08x picked (flags %x)", sentence.textId, sentence.flags);
	if (sentence.textId && sentence.soundId) {
		if (sentence.flags & kDialogAnimation)
			handleMessage(kEventObjActivateAnim, sentence.soundId, 0);
		else {
			sayPhrase(sentence.textId, sentence.soundId);
			return;
		}
	}
	// No spoken line: continue the flow at once
	if (_sentenceFlags & kDialogGoodbye)
		endDialog();
	else if (!_answerAnim)
		showDialogList();
}

// Sentence list at the bottom of the screen, one line per active
// sentence; the original renders them in the dialog font colors
// Trims a line to the available width appending an ellipsis, as the
// original ReduceStr does when a sentence does not fit the screen
static Common::U32String reduceStr(const Graphics::Font *font,
		const Common::U32String &text, int maxWidth) {
	if (font->getStringWidth(text) <= maxWidth)
		return text;
	const Common::U32String dots("...");
	const int dotsWidth = font->getStringWidth(dots);
	Common::U32String out;
	int width = 0;
	for (uint i = 0; i < text.size(); i++) {
		int charWidth = font->getCharWidth(text[i]);
		if (width + charWidth > maxWidth - dotsWidth)
			break;
		out += text[i];
		width += charWidth;
	}
	return out + dots;
}

void Logic::drawDialog(Graphics::Screen *screen) const {
	if (!_dialogOpen)
		return;

	const Graphics::Font *font = TextWriter::dialogFont();
	if (!font)
		return;

	byte bright = findNearestColor(255, 255, 5); // wcColorStandard
	byte hot = findNearestColor(255, 120, 0);     // wcColorHot
	byte dark = findNearestColor(5, 5, 5);       // wcColorShade

	const int lineHeight = 22; // WrChunkSize
	int top = screen->h - (int)_visibleSentences.size() * lineHeight - 4;
	// The sentence under the cursor highlights in the hot color
	Common::Point mouse = g_system->getEventManager()->getMousePos();
	int hovered = (mouse.y >= top && mouse.y < top + (int)_visibleSentences.size() * lineHeight)
		? (mouse.y - top) / lineHeight : -1;
	int y = top;
	for (uint i = 0; i < _visibleSentences.size(); i++, y += lineHeight) {
		const DialogSentence &sentence = _dialogs[_currentDialog][_visibleSentences[i]];
		const ResourceEntry *e = g_engine->resources().findByResId(sentence.textId);
		if (!e)
			continue;
		byte *data = g_engine->resources().readBlob(*e);
		if (!data)
			continue;
		uint32 textLen = 0;
		while (textLen < e->size && data[textLen] != '\0')
			textLen++;
		Common::U32String text(Common::String((const char *)data, textLen), Common::kISO8859_1);
		delete[] data;
		text = reduceStr(font, text, screen->w - 8);
		// Shadow printed twice as in the original ExtTextOut
		font->drawString(screen, text, 5, y + 1, screen->w - 8, dark);
		font->drawString(screen, text, 6, y + 2, screen->w - 8, dark);
		font->drawString(screen, text, 4, y, screen->w - 8,
			((int)i == hovered) ? hot : bright);
	}
}

bool Logic::handleDialogClick(const Common::Point &screenPos) {
	if (!_dialogOpen)
		return false;
	const int lineHeight = 22; // WrChunkSize
	int top = kScreenHeight - (int)_visibleSentences.size() * lineHeight - 4;
	int bottom = top + (int)_visibleSentences.size() * lineHeight;
	if (screenPos.y >= top && screenPos.y < bottom)
		pickSentence((screenPos.y - top) / lineHeight);
	return true;
}

} // End of namespace Gamebot
