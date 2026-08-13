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
	setObjectEnabled(objectId, false);
	debugC(kDebugActions, "Object %08x added to the inventory", objectId);
}

void Logic::setObjectEnabled(uint32 objectId, bool enabled) {
	_objectEnabled[objectId] = enabled;
	g_engine->world().setEnabled(objectId, enabled);
}

void Logic::applyObjectStates() {
	for (auto &entry : _objectEnabled)
		g_engine->world().setEnabled(entry._key, entry._value);
}

void Logic::syncGame(Common::Serializer &s) {
	// Inventory
	uint32 count = _inventory.size();
	s.syncAsUint32LE(count);
	if (s.isLoading()) {
		_inventory.clear();
		for (uint32 i = 0; i < count; i++) {
			uint32 id = 0;
			s.syncAsUint32LE(id);
			_inventory[id] = true;
		}
	} else {
		for (auto &entry : _inventory) {
			uint32 id = entry._key;
			s.syncAsUint32LE(id);
		}
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

	// Taking an object marked as takeable is built into the original
	// engine; the rule tables only cover the special cases
	if (verb == kVerbTake) {
		const ObjectEntry *object = g_engine->initialWorld().findObject(objectId);
		if (object && (object->flags & ObjectEntry::kFlagTakeable)) {
			addToInventory(objectId);
			return;
		}
	}

	if (!matched) {
		// No rule handled the verb: the original answers with a
		// generic "I can't do that" phrase chosen by the TCAU flags
		const ObjectEntry *object = g_engine->initialWorld().findObject(objectId);
		debugC(kDebugActions, "No rule for %s on %08x, generic response",
			eventName(eventId) ? eventName(eventId) : "?", objectId);
		sayGenericResponse(eventId, object ? object->impossibleResponses : 0);
	}
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
	g_engine->master().setTalking(_writer.active());
}

void Logic::update(uint32 millis) {
	// A phrase with a voice stays on screen while the voice plays
	if (_phraseSound && g_engine->sounds().isSoundPlaying())
		_writer.keepAlive(millis);

	bool phraseWasActive = _writer.active();
	_writer.update(millis);
	if (phraseWasActive && !_writer.active())
		onPhraseEnded();

	// Fire the pending verb when the character arrives
	if (_pendingActive && !g_engine->master().isWalking()) {
		_pendingActive = false;
		performVerb(_pendingObject, _pendingVerb, _pendingLinked);
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
		// A rule that names a second parameter (e.g. the inventory
		// object of a use-with) only fires on an exact match
		if (rule.eventParam2 && rule.eventParam2 != param2)
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
		if (!e)
			warning("Animation %08x not found", rule.actionParam1);
		else if (e->type == kResAnimationFlic)
			g_engine->playVideo(rule.actionParam1);
		else
			g_engine->world().startAnimation(e->objectId, rule.actionParam1);
		break;
	}
	case kActionEnable:
		setObjectEnabled(rule.actionParam1, true);
		break;
	case kActionDisable:
		setObjectEnabled(rule.actionParam1, false);
		break;
	case kActionPhraseOn:
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

void Logic::handleMessage(uint32 eventCode, uint32 param2, uint32 param3) {
	// State side effects of the well-known messages, then the event
	// is offered to the rule tables so chains keep running
	switch (eventCode) {
	case kEventObjToInventory:
		addToInventory(param2);
		break;
	case kEventObjDisable:
		setObjectEnabled(param2, false);
		break;
	case kEventObjEnable:
		setObjectEnabled(param2, true);
		break;
	case kEventObjActivateAnim: {
		const ResourceEntry *e = g_engine->resources().findByResId(param2);
		if (e && e->type == kResAnimationFlic)
			g_engine->playVideo(param2);
		else if (e)
			g_engine->world().startAnimation(e->objectId, param2);
		break;
	}
	case kEventDialogActivate:
		activateDialog(param2);
		break;
	case kEventAppPhaseChange:
		// The intro chain jumps to 0x99, the original main menu;
		// until that UI exists a new game starts at the chapter 1 map
		if (!g_engine->gotoPhase(param2)) {
			debugC(kDebugActions, "Phase %08x is not a room (menu?), starting chapter 1", param2);
			g_engine->gotoPhase(0x101);
		}
		return; // gotoPhase already ran any follow-up chain
	case kEventOptionsActivate:
		debugC(kDebugActions, "Options menu requested (TODO), starting chapter 1");
		g_engine->gotoPhase(0x101);
		return;
	case 0x09000002: // evDialogSetFrase: toggle a sentence by text id
		for (auto &dialog : _dialogs) {
			for (uint i = 0; i < dialog._value.size(); i++) {
				if (dialog._value[i].textId == param2) {
					if (param3)
						dialog._value[i].flags |= kDialogActive;
					else
						dialog._value[i].flags &= ~kDialogActive;
				}
			}
		}
		break;
	default:
		break;
	}
	dispatchEvent(eventCode, param2, param3);
}

void Logic::onPhraseEnded() {
	g_engine->master().setTalking(false);

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

void Logic::onAnimationEnded(uint32 resId) {
	if (_pendingAnswerAnim && resId == _pendingAnswerAnim) {
		_pendingAnswerAnim = 0;
		if (_sentenceFlags & kDialogGoodbye)
			endDialog();
		else
			showDialogList();
		return;
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

void Logic::activateDialog(uint32 dialogId) {
	if (!loadDialog(dialogId))
		return;
	_currentDialog = dialogId;
	_answerAnim = 0;
	_pendingAnswerAnim = 0;
	_sentenceFlags = 0;
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
	_dialogOpen = true;
	debugC(kDebugActions, "Dialog %08x offers %u sentences",
		_currentDialog, _visibleSentences.size());
}

void Logic::endDialog() {
	uint32 dialogId = _currentDialog;
	_currentDialog = 0;
	_dialogOpen = false;
	_visibleSentences.clear();
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
void Logic::drawDialog(Graphics::Screen *screen) const {
	if (!_dialogOpen)
		return;

	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!font)
		return;

	byte palette[256 * 3];
	g_system->getPaletteManager()->grabPalette(palette, 0, 256);
	int bright = 255, dark = 254, maxSum = -1, minSum = 999;
	for (int i = 1; i < 256; i++) {
		int sum = palette[i * 3] + palette[i * 3 + 1] + palette[i * 3 + 2];
		if (sum > maxSum) { maxSum = sum; bright = i; }
		if (sum < minSum) { minSum = sum; dark = i; }
	}

	const int lineHeight = font->getFontHeight() + 2;
	int y = screen->h - (int)_visibleSentences.size() * lineHeight - 4;
	for (uint i = 0; i < _visibleSentences.size(); i++, y += lineHeight) {
		const DialogSentence &sentence = _dialogs[_currentDialog][_visibleSentences[i]];
		const ResourceEntry *e = g_engine->resources().findByResId(sentence.textId);
		if (!e)
			continue;
		byte *data = g_engine->resources().readBlob(*e);
		if (!data)
			continue;
		Common::U32String text(Common::String((const char *)data, e->size), Common::kISO8859_1);
		delete[] data;
		font->drawString(screen, text, 11, y + 1, screen->w - 20, dark);
		font->drawString(screen, text, 10, y, screen->w - 20, bright);
	}
}

bool Logic::handleDialogClick(const Common::Point &screenPos) {
	if (!_dialogOpen)
		return false;
	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	const int lineHeight = (font ? font->getFontHeight() : 12) + 2;
	int top = kScreenHeight - (int)_visibleSentences.size() * lineHeight - 4;
	if (screenPos.y >= top)
		pickSentence((screenPos.y - top) / lineHeight);
	return true;
}

} // End of namespace Gamebot
