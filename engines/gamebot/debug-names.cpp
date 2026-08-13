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

#include "gamebot/debug-names.h"

namespace Gamebot {

struct CodeName {
	uint32 code;
	const char *name;
};

// Event codes of the original engine, extracted from GameBot's Events.h.
// The names match the original defines so traces can be compared 1:1
// against logs of the original engine built with _DEBUG.
static const CodeName kEventNames[] = {
	{ 0xFFFFFFFF, "evNothing" },
	{ 0xFFFFFFFE, "evRegToAll" },
	{ 0x00000001, "evEndProgram" },
	{ 0x10000000, "evCommand" },
	{ 0x11000000, "evMessage" },
	{ 0x01FFFFFF, "evRegister" },
	{ 0x01000001, "evRegisterReg" },
	{ 0x01000002, "evRegisterDereg" },
	{ 0x02FFFFFF, "evThread" },
	{ 0x02000001, "evThreadSuspend" },
	{ 0x02000002, "evThreadResume" },
	{ 0x02000004, "evThreadEnd" },
	{ 0x03FFFFFF, "evKey" },
	{ 0x03000001, "evKeyPressed" },
	{ 0x03000002, "evKeyReleased" },
	{ 0x03000004, "evKeyRegKey" },
	{ 0x03000008, "evKeyDeregKey" },
	{ 0x03000010, "evKeyDisable" },
	{ 0x03000020, "evKeyEnable" },
	{ 0x04FFFFFF, "evMouse" },
	{ 0x04000001, "evMouseMove" },
	{ 0x04000002, "evMouseLButtonDown" },
	{ 0x04000004, "evMouseLButtonUp" },
	{ 0x04000008, "evMouseRButtonDown" },
	{ 0x04000010, "evMouseRButtonUp" },
	{ 0x04000020, "evMouseDoubleClick" },
	{ 0x04000040, "evMouseSetMode" },
	{ 0x04000080, "evMouseSelectAction" },
	{ 0x04000100, "evMouseActionPalette" },
	{ 0x04000200, "evMouseSetCursor" },
	{ 0x04000400, "evMouseDisable" },
	{ 0x04000800, "evMouseEnable" },
	{ 0x04001000, "evMouseGetObject" },
	{ 0x05FFFFFF, "evTime" },
	{ 0x05000001, "evTimeRegTick" },
	{ 0x05000002, "evTimeDeregTick" },
	{ 0x05000004, "evTimeTick" },
	{ 0x05000008, "evTimeActivate" },
	{ 0x05000010, "evTimeDeactivate" },
	{ 0x05000020, "evTimeCleanTicks" },
	{ 0x06FFFFFF, "evVideo" },
	{ 0x06000001, "evVideoRedraw" },
	{ 0x06000002, "evVideoRedrawInv" },
	{ 0x06000004, "evVideoScroll" },
	{ 0x06000008, "evVideoSetCursor" },
	{ 0x06000010, "evVideoSetPhase" },
	{ 0x06000020, "evVideoCheckBounds" },
	{ 0x06004000, "evVideoRestorePalette" },
	{ 0x07FFFFFF, "evSound" },
	{ 0x07000001, "evSoundInitTrack" },
	{ 0x07000002, "evSoundStopTrack" },
	{ 0x07000004, "evSoundPlayPop" },
	{ 0x07000008, "evSoundStopPop" },
	{ 0x07000010, "evSoundPopStarted" },
	{ 0x07000020, "evSoundPopEnded" },
	{ 0x07000040, "evSoundChangeVolume" },
	{ 0x07000080, "evSoundPausePops" },
	{ 0x07000100, "evSoundStopAll" },
	{ 0x08FFFFFF, "evOptions" },
	{ 0x08000001, "evOptionsActivate" },
	{ 0x08000002, "evOptionsGetData" },
	{ 0x08000004, "evOptionsPushButton" },
	{ 0x08000008, "evOptionsValueChanged" },
	{ 0x08000010, "evOptionsMarkGained" },
	{ 0x09FFFFFF, "evDialog" },
	{ 0x09000001, "evDialogActivate" },
	{ 0x09000002, "evDialogSetFrase" },
	{ 0x09000004, "evDialogEnded" },
	{ 0x09000008, "evDialogReload" },
	{ 0x0AFFFFFF, "evText" },
	{ 0x0A000001, "evTextActivate" },
	{ 0x0A000002, "evTextDeactivate" },
	{ 0x0A000004, "evTextWriteCode" },
	{ 0x0A000008, "evTextWriteDialog" },
	{ 0x0A000010, "evTextWriteStr" },
	{ 0x0A000020, "evTextClean" },
	{ 0x0A000040, "evTextFullClean" },
	{ 0x0BFFFFFF, "evInventory" },
	{ 0x0B000001, "evInventoryActivate" },
	{ 0x0B000002, "evInventoryHasObj" },
	{ 0x0B000004, "evInventoryGetObj" },
	{ 0x0CFFFFFF, "evFX" },
	{ 0x0C000001, "evFXStartEffect" },
	{ 0x12FFFFFF, "evChange" },
	{ 0x12000001, "evChangeDisable" },
	{ 0x12000002, "evChangeEnable" },
	{ 0x12000004, "evChangeSetActive" },
	{ 0x0DFFFFFF, "evApp" },
	{ 0x0D000001, "evAppActivate" },
	{ 0x0D000002, "evAppPhaseChange" },
	{ 0x0D000004, "evAppLoadGame" },
	{ 0x0D000008, "evAppSaveGame" },
	{ 0x0EFFFFFF, "evObj" },
	{ 0x0E000001, "evObjToInventory" },
	{ 0x0E000002, "evObjActivateAnim" },
	{ 0x0E000004, "evObjAnimEnded" },
	{ 0x0E000010, "evObjDisable" },
	{ 0x0E000020, "evObjEnable" },
	{ 0x0E000040, "evObjSetInvImage" },
	{ 0x0E000080, "evObjChangeLayer" },
	{ 0x0E000100, "evObjEndAnimChain" },
	{ 0x0E000400, "evObjIsActive" },
	{ 0x0E000800, "evObjAutoExtract" },
	{ 0x0E001000, "evObjSayName" },
	{ 0x0E002000, "evObjUsarInvent" },
	{ 0x0E004000, "evObjGotoLayer" },
	{ 0x0E020000, "evObjHeLlegado" },
	{ 0x0E040000, "evObjLargarseYa" },
	{ 0x0E080000, "evObjHablarYa" },
	{ 0x0E100000, "evObjCogerYa" },
	{ 0x0E200000, "evObjMirarYa" },
	{ 0x0E400000, "evObjAbrirYa" },
	{ 0x0E800000, "evObjUsarYa" },
	{ 0x0FFFFFFF, "evPers" },
	{ 0x0F000020, "evPersActionTCAU" },
	{ 0x0F000040, "evPersAnimation" },
	{ 0x0F000080, "evPersHabla" },
	{ 0x0F000100, "evPersSetMaster" },
	{ 0x0F000200, "evPersTeleport" },
	{ 0x0F020000, "evPersVen" },
	{ 0x0F040000, "evPersVenLargarse" },
	{ 0x0F080000, "evPersVenHablar" },
	{ 0x0F100000, "evPersVenCoger" },
	{ 0x0F200000, "evPersVenMirar" },
	{ 0x0F400000, "evPersVenAbrir" },
	{ 0x0F800000, "evPersVenUsar" },
};

// Action codes from the original VisualObject.h
static const char *const kActionNames[] = {
	"actSendMsg",        // 0x0001
	"actStartAnimation", // 0x0002
	"actEnable",         // 0x0003
	"actDisable",        // 0x0004
	"actFraseOn",        // 0x0005
	"actInputEnable",    // 0x0006
};

// Condition codes from the original Actions.h (low byte selects the
// check, 0x0100 negates it, 0x0200 makes it an OR term)
static const char *const kConditionNames[] = {
	"IfEnabled",  // 0x01
	"IfInInvent", // 0x02
	"IfInBounds", // 0x03
	"IfLParam",   // 0x04
};

static const char *const kResourceTypeNames[] = {
	"unknown", "image", "hiddenImage", "mouseImage", "selectImage",
	"animAuto", "animAutoMobile", "animEvent", "animEventMobile",
	"inventoryImage", "phaseInit", "dialog", "sound", "music",
	"phaseExit", "phaseMap", "text", "mapExit", "fxSound", "flic",
};

const char *eventName(uint32 code) {
	for (uint i = 0; i < ARRAYSIZE(kEventNames); i++) {
		if (kEventNames[i].code == code)
			return kEventNames[i].name;
	}
	return nullptr;
}

const char *actionName(uint32 code) {
	if (code >= 1 && code <= ARRAYSIZE(kActionNames))
		return kActionNames[code - 1];
	return nullptr;
}

Common::String conditionName(uint16 code) {
	if (!code)
		return "none";
	Common::String result;
	if (code & 0x0200)
		result += "OR ";
	if (code & 0x0100)
		result += "NOT ";
	byte check = code & 0xff;
	if (check >= 1 && check <= ARRAYSIZE(kConditionNames))
		result += kConditionNames[check - 1];
	else
		result += Common::String::format("badCondition(%02x)", check);
	return result;
}

const char *resourceTypeName(ResourceType type) {
	if (type < kResTypeCount)
		return kResourceTypeNames[type];
	return "invalid";
}

} // End of namespace Gamebot
