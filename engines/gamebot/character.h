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

#ifndef GAMEBOT_CHARACTER_H
#define GAMEBOT_CHARACTER_H

#include "common/array.h"
#include "common/rect.h"

#include "gamebot/resource.h"

namespace Graphics {
class Screen;
}

namespace Gamebot {

class World;

// Object ids of the main characters (from the original MainClass.h)
enum CharacterId {
	kCharMortadelo = 0x99aa99,
	kCharFilemon = 0x99bb99,
	kCharBoth = 0x99cc99
};

// Orientations, clockwise from north (original or_* constants)
enum Orientation {
	kOrientNorth = 0,
	kOrientNorthEast,
	kOrientEast,
	kOrientSouthEast,
	kOrientSouth,
	kOrientSouthWest,
	kOrientWest,
	kOrientNorthWest
};

// Character resource id codes; resId = (objectId << 8) + code
enum CharacterResource {
	kResStaticFront = 0x01, // facing the player (E to W through S)
	kResStaticBack = 0x02,  // facing away (W to E through N)
	kResTalkFront = 0x10,   // talking animation
	kResTalkBack = 0x11,
	kResTakeCrouch = 0x12,  // take animations (chosen by object flags)
	kResTakeFront = 0x13,
	kResTakeAbove = 0x14,
	kResWalkBase = 0x20,    // + 1..8: walk animation per direction code
};

// Walk animation codes per direction (original PersAniMover*).
// Directions without an animation mirror another one horizontally.
enum WalkAnimCode {
	kWalkSouthWest = 0x21,
	kWalkNorthEast = 0x22,
	kWalkNorth = 0x23,
	kWalkSouth = 0x24,
	kWalkEast = 0x25,
	kWalkNorthWest = 0x26,
	kWalkSouthEast = 0x27,
	kWalkWest = 0x28
};

// Walking parameters from the original Character.cpp
constexpr int kStepSize = 15;        // pixels walked per tick at 100% scale
constexpr uint32 kWalkTickMs = 83;   // delay between walk frames

// One precomputed step of a walk: the original engine simulates the
// whole walk at CalcPath time and stores per-tick screen positions
struct WalkStep {
	int16 x = 0, y = 0;      // feet position (bottom-center anchor)
	uint16 layer = 0;        // phase layer while crossing this step
	uint16 orient = 0;
	uint16 scale = 1000;     // 1000 = 100%
};

class Character {
public:
	~Character();

	bool load(uint32 objectId);
	void setPosition(int16 x, int16 y, uint16 orient, uint16 layer);
	void enterPhase(const World &world, const CharacterLocation &location);

	// Computes the walk path to a target point (original CalcPath).
	// Returns false when there is no path at all.
	bool walkTo(const World &world, Common::Point target);

	void tick(uint32 millis, const World &world);
	void draw(Graphics::Screen *screen, const Common::Point &origin) const;

	// Plays or stops the talking animation (front or back depending
	// on the orientation, as in the original evPersHabla handling)
	void setTalking(bool talking);
	bool isTalking() const { return _talking; }

	// Plays a one-shot action animation (the take gestures); returns
	// false if the character lacks that animation
	bool playActionAnim(uint32 code);
	bool isActionAnimating() const { return _actionAnim != nullptr; }

	// Scene (event) animation: the original swaps the character's
	// active resource for it and draws it at absolute scene
	// coordinates until the end notification restores the sprite
	bool startSceneAnim(const ResourceEntry &e);
	bool sceneAnimActive() const { return _sceneAnim.active; }
	void finishSceneAnim(); // fast-forward to the end (click skip)

	uint32 objectId() const { return _objectId; }
	bool isLoaded() const { return _loaded; }
	bool isWalking() const { return _walking; }
	int16 x() const { return _x; }
	int16 y() const { return _y; }
	uint16 layer() const { return _layer; }
	uint16 orientation() const { return _orient; }
	uint16 scale() const { return _scale; }
	bool visible = true;

	// Remaining path, for the showpath debug overlay
	const Common::Array<WalkStep> &path() const { return _path; }
	uint pathPosition() const { return _pathPos; }

private:
	struct Sprite {
		Common::Rect rect;
		byte *pixels = nullptr;
	};

	struct WalkAnim {
		bool valid = false;
		Common::Rect rect;
		byte *frames = nullptr;
		uint32 frameSize = 0;
		uint32 imageCount = 0;
		uint32 framePeriod = 0;
		Common::Array<uint32> sequence; // frame indexes (1-based)
	};

	struct SceneAnim {
		bool active = false;
		uint32 resId = 0;
		Common::Rect rect;       // absolute phase coordinates
		byte *frames = nullptr;  // owned
		uint32 frameSize = 0;
		uint32 imageCount = 0;
		uint32 framePeriod = 0;
		Common::Array<SequenceStep> sequence;
		uint32 seqPos = 0;
		uint32 stepTime = 0;
	};
	SceneAnim _sceneAnim;
	void emitSceneStep() const;
	void endSceneAnim();

	bool loadAnimResource(const ResourceEntry &e, WalkAnim &anim);

	// Selects the walk animation for an orientation, setting _invert
	// when the direction mirrors another one (original SelectWalk)
	int selectWalkAnim(uint16 orient);
	void selectIdle();
	void drawScaled(Graphics::Screen *screen, const byte *pixels,
		const Common::Rect &srcRect, const Common::Point &origin) const;

	uint32 _objectId = 0;
	bool _loaded = false;

	Sprite _staticFront, _staticBack;
	WalkAnim _walkAnims[8]; // indexed by walk code - 0x21
	WalkAnim _talkFront, _talkBack;
	WalkAnim _takeAnims[3]; // crouch, front, above

	bool _talking = false;
	uint32 _talkStep = 0;
	uint32 _talkStepTime = 0;

	WalkAnim *_actionAnim = nullptr; // one-shot gesture in progress
	uint32 _actionStep = 0;
	uint32 _actionStepTime = 0;

	int16 _x = 0, _y = 0;
	uint16 _layer = 0, _orient = kOrientSouth;
	uint16 _scale = 1000;
	bool _invert = false;   // mirror horizontally when drawing

	bool _walking = false;
	Common::Array<WalkStep> _path;
	uint _pathPos = 0;
	int _currentAnim = -1;  // index into _walkAnims while walking
	uint32 _animStep = 0;   // position in the walk animation sequence
	uint32 _nextTick = 0;
};

} // End of namespace Gamebot

#endif // GAMEBOT_CHARACTER_H
