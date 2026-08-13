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
#include "common/system.h"
#include "graphics/fontman.h"
#include "graphics/font.h"
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

void TextWriter::showTextCode(uint32 textCode) {
	const ResourceEntry *e = g_engine->resources().findByResId(textCode);
	if (!e || e->type != kResText) {
		warning("Text code %08x not found", textCode);
		return;
	}
	byte *data = g_engine->resources().readBlob(*e);
	if (!data)
		return;
	showString(Common::String((const char *)data, e->size));
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
	if (_text.empty())
		return;

	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	if (!font)
		return;

	// The original wrote RGB text over the 8bpp scene via GDI; here
	// the brightest and darkest palette entries stand in for the
	// phrase color and its shade
	byte palette[256 * 3];
	g_system->getPaletteManager()->grabPalette(palette, 0, 256);
	int bright = 255, dark = 254, maxSum = -1, minSum = 999;
	for (int i = 1; i < 256; i++) {
		int sum = palette[i * 3] + palette[i * 3 + 1] + palette[i * 3 + 2];
		if (sum > maxSum) { maxSum = sum; bright = i; }
		if (sum < minSum) { minSum = sum; dark = i; }
	}

	const int width = screen->w - 40;
	Common::Array<Common::U32String> lines;
	font->wordWrapText(Common::U32String(_text, Common::kISO8859_1), width, lines);
	int y = 8;
	for (uint i = 0; i < lines.size(); i++) {
		font->drawString(screen, lines[i], 21, y + 2, width, dark, Graphics::kTextAlignCenter);
		font->drawString(screen, lines[i], 20, y, width, bright, Graphics::kTextAlignCenter);
		y += font->getFontHeight();
	}
}

void Logic::addToInventory(uint32 objectId) {
	_inventory[objectId] = true;
	g_engine->world().setEnabled(objectId, false);
	debugC(kDebugActions, "Object %08x added to the inventory", objectId);
}

static uint32 verbEvent(Verb verb) {
	switch (verb) {
	case kVerbTake: return kEventObjTakeNow;
	case kVerbTalk: return kEventObjTalkNow;
	case kVerbLook: return kEventObjLookNow;
	case kVerbOpen: return kEventObjOpenNow;
	case kVerbLeave: return kEventObjLeaveNow;
	default: return kEventObjUseNow;
	}
}

void Logic::interactWith(uint32 objectId, Verb verb) {
	const ObjectEntry *object = g_engine->initialWorld().findObject(objectId);
	if (!object)
		return;

	_pendingObject = objectId;
	_pendingVerb = verb;
	_pendingActive = true;

	// Walk to the object's interaction point; the verb runs on arrival
	Character &character = g_engine->mortadelo();
	if (object->targetX || object->targetY)
		character.walkTo(g_engine->world(), Common::Point(object->targetX, object->targetY));
	debugC(kDebugActions, "Walking to %08x '%s' for verb %d",
		objectId, object->name.c_str(), (int)verb);
}

void Logic::performVerb(uint32 objectId, Verb verb, uint32 linkedObjectId) {
	uint32 eventId = verbEvent(verb);
	uint matched = dispatchEvent(eventId, objectId, linkedObjectId);
	if (!matched) {
		// No rule handled the verb: the original answers with a
		// generic "I can't do that" phrase chosen by the TCAU flags
		debugC(kDebugActions, "No rule for %s on %08x (generic response pending)",
			eventName(eventId) ? eventName(eventId) : "?", objectId);
	}
}

void Logic::update(uint32 millis) {
	bool phraseWasActive = _writer.active();
	_writer.update(millis);
	if (phraseWasActive && !_writer.active())
		onPhraseEnded();

	// Fire the pending verb when the character arrives
	if (_pendingActive && !g_engine->mortadelo().isWalking()) {
		_pendingActive = false;
		performVerb(_pendingObject, _pendingVerb);
	}
}

uint Logic::dispatchEvent(uint32 eventId, uint32 param1, uint32 param2) {
	if (s_dispatchDepth > 8) {
		warning("Action rule chain too deep, stopping at %s", eventName(eventId));
		return 0;
	}
	s_dispatchDepth++;

	// Every object's rule table sees every event, as in the original
	// dispatch; a rule matches on the event id and its parameters
	ActionFile &actions = g_engine->actions();
	uint matched = 0;
	for (uint i = 0; i < actions.ruleCount(); i++) {
		const ActionRule &rule = actions.rule(i);
		if (rule.eventId != eventId || rule.eventParam1 != param1)
			continue;
		if (rule.eventParam2 && param2 && rule.eventParam2 != param2)
			continue;
		if (!checkConditions(rule))
			continue;
		debugC(kDebugActions, "Rule: on %s(%x,%x) do %s(%x,%x,%x)",
			eventName(eventId) ? eventName(eventId) : "?", param1, param2,
			actionName(rule.actionId) ? actionName(rule.actionId) : "?",
			rule.actionParam1, rule.actionParam2, rule.actionParam3);
		runAction(rule);
		matched++;
	}
	s_dispatchDepth--;
	return matched;
}

bool Logic::checkConditions(const ActionRule &rule) {
	// All conditions must hold. TODO: OR terms (0x200) are treated as
	// AND for now; no rule chain observed so far depends on them.
	for (uint c = 0; c < 6; c++) {
		uint16 condition = rule.conditions[c];
		if (!condition)
			continue;
		bool result = true;
		uint32 arg = rule.condArgs[c];
		switch (condition & 0xff) {
		case kIfEnabled:
			result = g_engine->world().isEnabled(arg);
			break;
		case kIfInInventory:
			result = isInInventory(arg);
			break;
		case kIfInBounds:
			result = true; // TODO: mouse-in-rect check
			break;
		case kIfLParam:
			result = true; // TODO: linked-object parameter check
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

void Logic::runAction(const ActionRule &rule) {
	switch (rule.actionId) {
	case kActionSendMsg:
		handleMessage(rule.actionParam1, rule.actionParam2, rule.actionParam3);
		break;
	case kActionStartAnimation: {
		const ResourceEntry *e = g_engine->resources().findByResId(rule.actionParam1);
		if (e)
			g_engine->world().startAnimation(e->objectId, rule.actionParam1);
		else
			warning("Animation %08x not found", rule.actionParam1);
		break;
	}
	case kActionEnable:
		g_engine->world().setEnabled(rule.actionParam1, true);
		break;
	case kActionDisable:
		g_engine->world().setEnabled(rule.actionParam1, false);
		break;
	case kActionPhraseOn:
		_writer.showTextCode(rule.actionParam1);
		if (rule.actionParam2) {
			debugC(kDebugSound, "Phrase wants voice %08x", rule.actionParam2);
			_phraseSound = rule.actionParam2;
		}
		break;
	case kActionInputEnable:
		debugC(kDebugActions, "Input enable %x (TODO)", rule.actionParam1);
		break;
	default:
		warning("Unknown action %x", rule.actionId);
		break;
	}
}

void Logic::handleMessage(uint32 eventCode, uint32 param2, uint32 param3) {
	// State side effects of the well-known messages, then the event
	// is offered to the rule tables so chains keep running
	switch (eventCode) {
	case kEventObjToInventory:
		addToInventory(param2);
		break;
	case kEventObjDisable:
		g_engine->world().setEnabled(param2, false);
		break;
	case kEventObjEnable:
		g_engine->world().setEnabled(param2, true);
		break;
	case kEventObjActivateAnim: {
		const ResourceEntry *e = g_engine->resources().findByResId(param2);
		if (e)
			g_engine->world().startAnimation(e->objectId, param2);
		break;
	}
	case kEventDialogActivate:
		debugC(kDebugActions, "Dialog %08x requested (TODO: DialogMaster)", param2);
		break;
	default:
		break;
	}
	dispatchEvent(eventCode, param2, param3);
}

void Logic::onPhraseEnded() {
	// The original signals the end of a spoken phrase with an
	// AnimEnded event carrying the voice code, which rules use to
	// chain reactions
	if (_phraseSound) {
		uint32 sound = _phraseSound;
		_phraseSound = 0;
		dispatchEvent(kEventObjAnimEnded, sound, 0);
	}
}

void Logic::onAnimationEnded(uint32 resId) {
	dispatchEvent(kEventObjAnimEnded, resId, 0);
}

} // End of namespace Gamebot
