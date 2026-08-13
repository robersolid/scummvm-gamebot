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

#ifndef GAMEBOT_LOGIC_H
#define GAMEBOT_LOGIC_H

#include "common/hashmap.h"
#include "common/str.h"

#include "gamebot/resource.h"

namespace Graphics {
class Screen;
}

namespace Gamebot {

// Event codes used by the action tables (from the original Events.h)
enum EventCode {
	kEventDialogActivate = 0x09000001,
	kEventObjToInventory = 0x0E000001,
	kEventObjActivateAnim = 0x0E000002,
	kEventObjAnimEnded = 0x0E000004,
	kEventObjDisable = 0x0E000010,
	kEventObjEnable = 0x0E000020,
	kEventObjUsarInvent = 0x0E002000,
	kEventObjArrived = 0x0E020000,
	kEventObjLeaveNow = 0x0E040000,
	kEventObjTalkNow = 0x0E080000,
	kEventObjTakeNow = 0x0E100000,
	kEventObjLookNow = 0x0E200000,
	kEventObjOpenNow = 0x0E400000,
	kEventObjUseNow = 0x0E800000
};

// Action codes (original VisualObject.h)
enum ActionCode {
	kActionSendMsg = 1,
	kActionStartAnimation,
	kActionEnable,
	kActionDisable,
	kActionPhraseOn,
	kActionInputEnable
};

// Condition codes (original Actions.h); 0x100 negates, 0x200 = OR term
enum ConditionCode {
	kIfEnabled = 0x01,
	kIfInInventory = 0x02,
	kIfInBounds = 0x03,
	kIfLParam = 0x04,
	kConditionNot = 0x100,
	kConditionOr = 0x200
};

// The verbs of the action palette
enum Verb {
	kVerbUse,
	kVerbTake,
	kVerbTalk,
	kVerbLook,
	kVerbOpen,
	kVerbLeave
};

// Dialog sentence flags (original DialogMaster.cpp)
enum DialogFlags {
	kDialogGoodbye = 0x0001,    // ends the conversation after this line
	kDialogAnimation = 0x0002,  // trigger an animation instead of speaking
	kDialogAnswer = 0x0010,     // an answer animation follows the line
	kDialogDeactivate = 0x0100, // the line disappears once used
	kDialogActive = 0x1000
};

struct DialogSentence {
	uint32 textId = 0;
	uint32 soundId = 0;
	uint32 animId = 0;
	uint32 flags = 0;
};

// On-screen phrase display (stand-in for the original WriterMaster,
// which rendered with a Windows GDI font)
class TextWriter {
public:
	void showTextCode(uint32 textCode);
	void showString(const Common::String &text);
	void update(uint32 millis);
	void draw(Graphics::Screen *screen) const;
	bool active() const { return !_text.empty(); }

private:
	Common::String _text;
	uint32 _hideTime = 0;
};

// Runs the data-driven action rules of Actions.act and keeps the
// mutable game state they touch: the inventory and the per-object
// enabled state
class Logic {
public:
	// Walks the character to an object and performs a verb on arrival;
	// linkedObjectId carries the inventory object of a use-with
	void interactWith(uint32 objectId, Verb verb, uint32 linkedObjectId = 0);
	// Immediately dispatches a verb event to an object's rule table
	void performVerb(uint32 objectId, Verb verb, uint32 linkedObjectId = 0);
	void update(uint32 millis);

	bool isInInventory(uint32 objectId) const { return _inventory.contains(objectId); }
	void addToInventory(uint32 objectId);
	const Common::HashMap<uint32, bool> &inventory() const { return _inventory; }

	TextWriter &writer() { return _writer; }

	// Shows a phrase spoken by the master character: text on screen,
	// talking animation and (eventually) the voice sample
	void sayPhrase(uint32 textCode, uint32 soundCode);

	// Conversations (original DialogMaster): a dialog resource holds
	// sentences the player can pick; a picked line is spoken, may run
	// an answer animation and reopens the list until a goodbye line
	void activateDialog(uint32 dialogId);
	void pickSentence(uint index);
	bool isDialogOpen() const { return _dialogOpen; }
	void drawDialog(Graphics::Screen *screen) const;
	bool handleDialogClick(const Common::Point &screenPos);

	// Chain notifications
	void onPhraseEnded();
	void onAnimationEnded(uint32 resId);

private:
	// Dispatches an event through the rule table of an object.
	// Returns the number of rules that matched and ran.
	uint dispatchEvent(uint32 eventId, uint32 param1, uint32 param2);
	bool checkConditions(const ActionRule &rule);
	void runAction(const ActionRule &rule);
	void handleMessage(uint32 eventCode, uint32 param2, uint32 param3);
	void sayGenericResponse(uint32 verbEventId, uint16 responseFlags);

	// Loads a dialog's sentences, preferring the state in default.dlg
	// over the pristine copy in the resource file
	bool loadDialog(uint32 dialogId);
	void showDialogList();
	void endDialog();

	Common::HashMap<uint32, bool> _inventory;
	TextWriter _writer;
	uint32 _phraseSound = 0; // voice code of the phrase on screen

	// Conversation state; sentence activation persists per dialog
	Common::HashMap<uint32, Common::Array<DialogSentence> > _dialogs;
	uint32 _currentDialog = 0;
	bool _dialogOpen = false;      // the sentence list is on screen
	Common::Array<uint> _visibleSentences; // indexes of listed lines
	uint32 _answerAnim = 0;        // NPC answer to run after the line
	uint32 _pendingAnswerAnim = 0; // waiting for this anim to finish
	uint32 _sentenceFlags = 0;     // flags of the picked line

	// Pending walk-then-act interaction
	uint32 _pendingObject = 0;
	uint32 _pendingLinked = 0;
	Verb _pendingVerb = kVerbUse;
	bool _pendingActive = false;
};

} // End of namespace Gamebot

#endif // GAMEBOT_LOGIC_H
