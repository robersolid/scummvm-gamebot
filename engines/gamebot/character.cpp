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
#include "common/endian.h"
#include "graphics/screen.h"

#include "gamebot/character.h"
#include "gamebot/gamebot.h"
#include "gamebot/world.h"

namespace Gamebot {

static const byte kTransparentColor = 0;

// Work map cell marks of the original path filler
static const byte kPathInvalid = 0xff;
static const byte kPathEmpty = 0x00;
static const byte kPathStart = 0x01;
static const byte kPathTarget = 0xfe;

// Neighbor displacements, straight directions first (original order)
static const int8 kNeighbors[8][2] = {
	{ 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 },
	{ 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 }
};

// Signed per-cell Y displacement (original Desplazar)
static int cellDisplacement(byte cell) {
	int value = cell & kCellDisplacementMask;
	return (cell & kCellDisplacementSign) ? -value : value;
}

Character::~Character() {
	delete[] _staticFront.pixels;
	delete[] _staticBack.pixels;
	delete[] _talkFront.frames;
	delete[] _talkBack.frames;
	for (uint i = 0; i < 8; i++)
		delete[] _walkAnims[i].frames;
}

bool Character::loadAnimResource(const ResourceEntry &e, WalkAnim &anim) {
	ResourceFile &res = g_engine->resources();
	byte *data = res.readBlob(e);
	if (!data)
		return false;
	anim.rect = Common::Rect(
		READ_LE_INT32(data), READ_LE_INT32(data + 4),
		READ_LE_INT32(data + 8) + 1, READ_LE_INT32(data + 12) + 1);
	anim.frameSize = anim.rect.width() * anim.rect.height();
	anim.imageCount = READ_LE_UINT32(data + 16);
	uint32 sequenceCount = READ_LE_UINT32(data + 20);
	anim.framePeriod = READ_LE_UINT32(data + 24);
	uint32 imageLocation = READ_LE_UINT32(data + 32);

	anim.sequence.resize(sequenceCount);
	for (uint32 s = 0; s < sequenceCount; s++)
		anim.sequence[s] = READ_LE_UINT32(data + 36 + s * 12);
	delete[] data;

	anim.frames = new byte[anim.frameSize * anim.imageCount];
	Common::File &file = res.file();
	file.seek(imageLocation);
	if (file.read(anim.frames, anim.frameSize * anim.imageCount) !=
			anim.frameSize * anim.imageCount) {
		warning("Could not read frames of %08x", e.resId);
		delete[] anim.frames;
		anim.frames = nullptr;
		return false;
	}
	anim.valid = true;
	return true;
}

void Character::setTalking(bool talking) {
	_talking = talking;
	_talkStep = 0;
	_talkStepTime = 0;
}

bool Character::load(uint32 objectId) {
	ResourceFile &res = g_engine->resources();
	_objectId = objectId;

	int i = res.findObject(objectId);
	if (i < 0) {
		warning("Character %08x has no resources", objectId);
		return false;
	}

	for (; i < (int)res.count() && res.entry(i).objectId == objectId; i++) {
		const ResourceEntry &e = res.entry(i);
		// Own resources use the (objectId << 8) + code id scheme
		if ((e.resId >> 8) != objectId)
			continue;
		uint32 code = e.resId & 0xff;

		if (e.type == kResImage && (code == kResStaticFront || code == kResStaticBack)) {
			byte *data = res.readBlob(e);
			if (!data)
				continue;
			Sprite &sprite = (code == kResStaticFront) ? _staticFront : _staticBack;
			sprite.rect = Common::Rect(
				READ_LE_INT32(data), READ_LE_INT32(data + 4),
				READ_LE_INT32(data + 8) + 1, READ_LE_INT32(data + 12) + 1);
			sprite.pixels = new byte[sprite.rect.width() * sprite.rect.height()];
			memcpy(sprite.pixels, data + 16, sprite.rect.width() * sprite.rect.height());
			delete[] data;
			continue;
		}

		bool isAnimation = e.type >= kResAnimationAuto && e.type <= kResAnimationEventMobile;
		if (isAnimation && code >= kWalkSouthWest && code <= kWalkWest)
			loadAnimResource(e, _walkAnims[code - kWalkSouthWest]);
		else if (isAnimation && code == kResTalkFront)
			loadAnimResource(e, _talkFront);
		else if (isAnimation && code == kResTalkBack)
			loadAnimResource(e, _talkBack);
	}

	if (!_staticFront.pixels) {
		warning("Character %08x has no static image", objectId);
		return false;
	}
	debugC(kDebugWalk, "Character %08x loaded (%d walk animations)",
		objectId, (int)(_walkAnims[0].valid + _walkAnims[1].valid + _walkAnims[2].valid +
		_walkAnims[3].valid + _walkAnims[4].valid + _walkAnims[5].valid +
		_walkAnims[6].valid + _walkAnims[7].valid));
	_loaded = true;
	return true;
}

void Character::setPosition(int16 x, int16 y, uint16 orient, uint16 layer) {
	_x = x;
	_y = y;
	_orient = orient;
	_layer = layer;
	_walking = false;
	_path.clear();
	_pathPos = 0;
	selectIdle();
}

void Character::enterPhase(const World &world, const CharacterLocation &location) {
	setPosition(location.x, location.y, location.orientation, location.layer);
	// Scale comes from the walk band under the entry position
	int row = world.walkRowAt(Common::Point(_x, _y));
	_scale = (row >= 0) ? world.mapScale(row) : 1000;
	debugC(kDebugWalk, "Character %08x enters at (%d,%d) or%u L%u scale %u",
		_objectId, _x, _y, _orient, _layer, _scale);
}

// Original SelectWalk: directions without an own animation mirror
// another one. Mirroring pairs: W->E, NW->NE, SE->SW.
int Character::selectWalkAnim(uint16 orient) {
	struct Selection {
		byte code;
		byte mirrorOf;
	};
	static const Selection kSelection[8] = {
		{ kWalkNorth, 0 },                  // N
		{ kWalkNorthEast, 0 },              // NE
		{ kWalkEast, 0 },                   // E
		{ kWalkSouthEast, kWalkSouthWest }, // SE
		{ kWalkSouth, 0 },                  // S
		{ kWalkSouthWest, 0 },              // SW
		{ kWalkWest, kWalkEast },           // W
		{ kWalkNorthWest, kWalkNorthEast }, // NW
	};

	const Selection &sel = kSelection[orient & 7];
	_invert = false;
	int index = sel.code - kWalkSouthWest;
	if (!_walkAnims[index].valid && sel.mirrorOf) {
		index = sel.mirrorOf - kWalkSouthWest;
		_invert = true;
	}
	return _walkAnims[index].valid ? index : -1;
}

// Original SelectStatic: front image for E..W through S, back image
// otherwise; western orientations mirror the image
void Character::selectIdle() {
	_currentAnim = -1;
	_animStep = 0;
	_invert = _orient > kOrientSouth;
}

// Straight port of Character::FindSquare
static Common::Point findSquare(const World &world, Common::Point pos) {
	Common::Point p;
	p.x = MIN<int>(pos.x / kWalkCellWidth, world.mapWidth() - 1);
	for (p.y = 0; p.y < (int)world.mapHeight(); p.y++) {
		int disp = cellDisplacement(world.mapCell(p.x, p.y));
		if (pos.y - disp <= world.mapBaseY(p.y))
			break;
	}
	if (p.y >= (int)world.mapHeight())
		p.y = world.mapHeight() - 1;
	return p;
}

// Straight port of FillMap: breadth-first flood fill with distance
// values; returns the distance at which the target was reached
static int fillMap(byte *workMap, uint width, uint height) {
	int noHang = 1000;
	for (int steps = 1; steps < 254 && noHang--; steps++) {
		for (uint y = 0; y < height; y++) {
			for (uint x = 0; x < width; x++) {
				if (workMap[x + y * width] != steps)
					continue;
				byte fill = steps + 1;
				for (int dir = 0; dir < 8; dir++) {
					if ((x == 0 && kNeighbors[dir][0] < 0) || (x >= width - 1 && kNeighbors[dir][0] > 0) ||
						(y == 0 && kNeighbors[dir][1] < 0) || (y >= height - 1 && kNeighbors[dir][1] > 0))
						continue;
					uint at = x + kNeighbors[dir][0] + (y + kNeighbors[dir][1]) * width;
					if (workMap[at] == kPathTarget) {
						workMap[at] = fill;
						return fill;
					}
					if (workMap[at] == kPathEmpty)
						workMap[at] = fill;
				}
			}
		}
	}
	return 0;
}

// Straight port of Character::UpdatePos: moves the position one step
// toward the target cell, axis by axis, and returns the orientation
// of the movement (-1 when the cell has been reached)
static int updatePos(Common::Point &pos, int stepX, int stepY, const Common::Point &targetCell,
		const World &world) {
	if (!stepX && !stepY)
		return 0;
	Common::Point target(targetCell.x * kWalkCellWidth, world.mapBaseY(targetCell.y));
	int orient = -1;

	if (stepY && (pos.y - stepY > target.y)) {
		pos.y -= stepY;
		orient = kOrientNorth;
	} else if (stepY && (pos.y + stepY < target.y)) {
		pos.y += stepY;
		orient = kOrientSouth;
	}
	if (stepX && (pos.x + stepX < target.x)) {
		pos.x += stepX;
		orient = kOrientEast;
		if (ABS(stepY) > 3 && pos.y > target.y)
			orient = kOrientNorthEast;
		else if (ABS(stepY) > 3 && pos.y < target.y)
			orient = kOrientSouthEast;
	} else if (stepX && (pos.x - stepX > target.x)) {
		pos.x -= stepX;
		orient = kOrientWest;
		if (ABS(stepY) > 3 && pos.y > target.y)
			orient = kOrientNorthWest;
		else if (ABS(stepY) > 3 && pos.y < target.y)
			orient = kOrientSouthWest;
	}
	return orient;
}

bool Character::walkTo(const World &world, Common::Point target) {
	if (!world.hasWalkMap() || !_loaded)
		return false;

	const uint width = world.mapWidth(), height = world.mapHeight();
	Common::Array<byte> workMap;
	workMap.resize(width * height);
	for (uint i = 0; i < width * height; i++)
		workMap[i] = (world.mapCellByIndex(i) & kCellWalkable) ? kPathEmpty : kPathInvalid;

	Common::Point dest = findSquare(world, target);
	Common::Point org = findSquare(world, Common::Point(_x, _y));
	debugC(kDebugWalk, "Walk from (%d,%d) cell (%d,%d) to (%d,%d) cell (%d,%d)",
		_x, _y, org.x, org.y, target.x, target.y, dest.x, dest.y);

	// Slide an unreachable destination toward the origin
	int unblock = 1000;
	while (workMap[dest.x + dest.y * width] == kPathInvalid && unblock--) {
		if (dest.x > org.x) dest.x--;
		if (dest.y > org.y) dest.y--;
		if (dest.x < org.x) dest.x++;
		if (dest.y < org.y) dest.y++;
	}
	if (unblock <= 0)
		return false;

	WalkStep endLocation;
	endLocation.x = dest.x * kWalkCellWidth;
	endLocation.y = world.mapBaseY(dest.y);
	endLocation.layer = height + 2 - dest.y;
	endLocation.orient = kOrientSouth;
	endLocation.scale = world.mapScale(dest.y);

	workMap[org.x + org.y * width] = kPathStart;
	workMap[dest.x + dest.y * width] = kPathTarget;

	int steps = fillMap(workMap.data(), width, height);
	_path.clear();
	_pathPos = 0;

	if (!steps) {
		// Already in the destination cell: nothing to walk
		selectIdle();
		_walking = false;
		return true;
	}

	// Backtrack the distance field into the cell path (start-first
	// after the loop since cells are collected dest-first)
	Common::Array<Common::Point> cells;
	cells.push_back(dest);
	unblock = 1000;
	for (; steps > 1 && unblock--; steps--) {
		for (int dir = 0; dir < 8; dir++) {
			if ((dest.x == 0 && kNeighbors[dir][0] < 0) || (dest.x >= (int)width - 1 && kNeighbors[dir][0] > 0) ||
				(dest.y == 0 && kNeighbors[dir][1] < 0) || (dest.y >= (int)height - 1 && kNeighbors[dir][1] > 0))
				continue;
			if (workMap[dest.x + kNeighbors[dir][0] + (dest.y + kNeighbors[dir][1]) * width] == steps - 1) {
				dest.x += kNeighbors[dir][0];
				dest.y += kNeighbors[dir][1];
				cells.push_back(dest);
				break;
			}
		}
	}

	// Simulate the walk cell by cell, producing one screen position
	// per tick (original CalcPath second half). Cells are consumed
	// nearest-first, so iterate the collected list backwards.
	Common::Point pos(_x, _y);
	int stepX = 0, stepY = 0;
	int orient = _orient;
	for (int c = (int)cells.size() - 1; c > 0; c--) {
		const Common::Point &cur = cells[c];
		const Common::Point &next = cells[c - 1];
		stepX = kStepSize * world.mapScale(cur.y) / 1000;
		stepY = stepX * ABS((int)world.mapBaseY(cur.y) - (int)world.mapBaseY(next.y)) / kWalkCellWidth;
		uint16 layer = height + 2 - cur.y;

		int guard = 1000;
		while (guard--) {
			int moveX = ABS(stepX * (cur.x - next.x));
			int result = updatePos(pos, moveX, stepY, next, world);
			if (result == -1 || result == 0)
				break;
			orient = result;

			WalkStep step;
			step.x = (int16)pos.x;
			step.orient = (uint16)orient;
			step.layer = layer;
			int scale = world.mapScale(cur.y);
			if (cur.y != next.y && world.mapBaseY(next.y) != world.mapBaseY(cur.y))
				scale += ((int)world.mapScale(next.y) - scale) *
					(pos.y - (int)world.mapBaseY(cur.y)) /
					((int)world.mapBaseY(next.y) - (int)world.mapBaseY(cur.y));
			step.scale = (uint16)CLIP(scale, 10, 2000);
			step.y = (int16)(pos.y + cellDisplacement(world.mapCell(cur.x, cur.y)));
			_path.push_back(step);
		}
	}

	endLocation.orient = orient;
	_path.push_back(endLocation);

	_walking = true;
	_currentAnim = selectWalkAnim(_path[0].orient);
	_animStep = 0;
	_nextTick = 0;
	debugC(kDebugWalk, "Path has %u steps, final (%d,%d) L%u scale %u",
		_path.size(), endLocation.x, endLocation.y, endLocation.layer, endLocation.scale);
	return true;
}

void Character::tick(uint32 millis, const World &world) {
	// Talking animation runs on its own clock while a phrase shows
	if (_talking) {
		const WalkAnim &anim = (_orient < kOrientEast || _orient > kOrientWest)
			? _talkBack : _talkFront;
		if (anim.valid && !anim.sequence.empty()) {
			if (!_talkStepTime)
				_talkStepTime = millis + anim.framePeriod;
			while (millis >= _talkStepTime) {
				_talkStepTime += anim.framePeriod ? anim.framePeriod : 100;
				_talkStep++;
				if (_talkStep >= anim.sequence.size() ||
						anim.sequence[_talkStep] >= SequenceStep::kAutoDisable)
					_talkStep = 0;
			}
		}
	}

	if (!_walking)
		return;
	if (!_nextTick)
		_nextTick = millis + kWalkTickMs;

	while (millis >= _nextTick && _walking) {
		_nextTick += kWalkTickMs;

		if (_pathPos >= _path.size()) {
			_walking = false;
			selectIdle();
			debugC(kDebugWalk, "Character %08x arrived at (%d,%d)", _objectId, _x, _y);
			break;
		}

		const WalkStep &step = _path[_pathPos++];
		_x = step.x;
		_y = step.y;
		_layer = step.layer;
		_scale = step.scale;
		if (step.orient != _orient) {
			_orient = step.orient;
			_currentAnim = selectWalkAnim(_orient);
			_animStep = 0;
		}

		// One animation frame per walk tick
		if (_currentAnim >= 0) {
			const WalkAnim &anim = _walkAnims[_currentAnim];
			_animStep++;
			if (_animStep >= anim.sequence.size() ||
					anim.sequence[_animStep] >= SequenceStep::kAutoDisable)
				_animStep = 0;
		}
	}
}

// Scaled, optionally mirrored blit anchored at the feet position
// (original Character::Redraw geometry)
void Character::drawScaled(Graphics::Screen *screen, const byte *pixels,
		const Common::Rect &srcRect, const Common::Point &origin) const {
	const int srcW = srcRect.width(), srcH = srcRect.height();
	const int dstW = MAX(1, srcW * _scale / 1000);
	const int dstH = MAX(1, srcH * _scale / 1000);

	Common::Rect dest(_x - dstW / 2, _y - dstH, _x - dstW / 2 + dstW, _y);
	dest.translate(-origin.x, -origin.y);
	Common::Rect clipped(dest);
	clipped.clip(Common::Rect(0, 0, screen->w, screen->h));
	debugC(3, kDebugWalk, "drawScaled src %dx%d dst (%d,%d,%d,%d) clip (%d,%d,%d,%d)",
		srcW, srcH, dest.left, dest.top, dest.right, dest.bottom,
		clipped.left, clipped.top, clipped.right, clipped.bottom);
	if (clipped.isEmpty())
		return;

	for (int dy = clipped.top; dy < clipped.bottom; dy++) {
		int sy = (dy - dest.top) * srcH / dstH;
		byte *dst = (byte *)screen->getBasePtr(clipped.left, dy);
		for (int dx = clipped.left; dx < clipped.right; dx++, dst++) {
			int sx = (dx - dest.left) * srcW / dstW;
			if (_invert)
				sx = srcW - 1 - sx;
			byte pixel = pixels[sy * srcW + sx];
			if (pixel != kTransparentColor)
				*dst = pixel;
		}
	}
}

void Character::draw(Graphics::Screen *screen, const Common::Point &origin) const {
	if (!_loaded || !visible)
		return;

	if (_walking && _currentAnim >= 0) {
		const WalkAnim &anim = _walkAnims[_currentAnim];
		uint32 imageIndex = anim.sequence.empty() ? 1 : anim.sequence[_animStep];
		if (imageIndex < 1 || imageIndex > anim.imageCount)
			imageIndex = 1;
		drawScaled(screen, anim.frames + (imageIndex - 1) * anim.frameSize, anim.rect, origin);
		return;
	}

	if (_talking) {
		const WalkAnim &anim = (_orient < kOrientEast || _orient > kOrientWest)
			? _talkBack : _talkFront;
		if (anim.valid) {
			uint32 imageIndex = anim.sequence.empty() ? 1 : anim.sequence[_talkStep];
			if (imageIndex < 1 || imageIndex > anim.imageCount)
				imageIndex = 1;
			drawScaled(screen, anim.frames + (imageIndex - 1) * anim.frameSize, anim.rect, origin);
			return;
		}
	}

	const Sprite &sprite = (_orient < kOrientEast || _orient > kOrientWest)
		? (_staticBack.pixels ? _staticBack : _staticFront) : _staticFront;
	drawScaled(screen, sprite.pixels, sprite.rect, origin);
}

} // End of namespace Gamebot
