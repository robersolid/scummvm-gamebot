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
#include "common/debug.h"
#include "common/memstream.h"
#include "common/substream.h"
#include "common/system.h"

#include "gamebot/gamebot.h"
#include "gamebot/sound.h"

namespace Gamebot {

// Original SoundSys conversion parameters
static const int kSampleRate = 22050;
static const uint32 kBlockAlign = 512;

SoundManager::~SoundManager() {
	stopAll();
}

bool SoundManager::playSound(uint32 resId, Audio::Mixer::SoundType type, uint32 objectId) {
	ResourceFile &res = g_engine->resources();
	const ResourceEntry *e = res.findByResId(resId);
	if (!e || (e->type != kResSound && e->type != kResFXSound)) {
		debugC(kDebugSound, "Sound %08x not found", resId);
		return false;
	}

	byte *data = res.readBlob(*e);
	if (!data)
		return false;

	// The volume category follows the resource type, as the original
	// mixer picks the slider from it: aSound goes to voices, aFXSound
	// to the effects
	type = (e->type == kResFXSound) ? Audio::Mixer::kSFXSoundType
		: Audio::Mixer::kSpeechSoundType;

	Common::MemoryReadStream *memory =
		new Common::MemoryReadStream(data, e->size, DisposeAfterUse::YES);
	Audio::RewindableAudioStream *stream = Audio::makeADPCMStream(
		memory, DisposeAfterUse::YES, e->size, Audio::kADPCMMS, kSampleRate, 1, kBlockAlign);
	if (!stream) {
		debugC(kDebugSound, "Failed to create ADPCM stream for sound %08x", resId);
		return false;
	}

	// Every pop mixes with the others, as the original DirectSound
	// buffers do; each keeps its own handle for state queries
	WatchedSound watched;
	watched.resId = resId;
	watched.objectId = objectId;
	g_engine->_mixer->playStream(type, &watched.handle, stream);
	_soundHandle = watched.handle;
	_watched.push_back(watched);
	debugC(kDebugSound, "Playing sound %08x (%u bytes)", resId, e->size);
	return true;
}

bool SoundManager::isSoundPlaying(uint32 resId) const {
	for (uint i = 0; i < _watched.size(); i++) {
		if (_watched[i].resId == resId &&
				g_engine->_mixer->isSoundHandleActive(_watched[i].handle))
			return true;
	}
	return false;
}

void SoundManager::stopSound(uint32 resId) {
	for (uint i = 0; i < _watched.size(); i++) {
		if (_watched[i].resId == resId)
			g_engine->_mixer->stopHandle(_watched[i].handle);
	}
}

void SoundManager::stopSoundByObject(uint32 objectId) {
	if (!objectId)
		return;
	for (uint i = 0; i < _watched.size(); i++) {
		if (_watched[i].objectId == objectId)
			g_engine->_mixer->stopHandle(_watched[i].handle);
	}
}

void SoundManager::pollFinishedSounds(Common::Array<uint32> &finished) {
	for (uint i = 0; i < _watched.size();) {
		if (!g_engine->_mixer->isSoundHandleActive(_watched[i].handle)) {
			finished.push_back(_watched[i].resId);
			_watched.remove_at(i);
		} else {
			i++;
		}
	}
}

bool SoundManager::isSoundPlaying() const {
	return g_engine->_mixer->isSoundHandleActive(_soundHandle);
}

void SoundManager::stopSound() {
	g_engine->_mixer->stopHandle(_soundHandle);
}

bool SoundManager::playMusic(uint32 resId) {
	if (_currentMusic == resId && g_engine->_mixer->isSoundHandleActive(_musicHandle))
		return true;
	stopMusic();

	ResourceFile &res = g_engine->resources();
	const ResourceEntry *e = res.findByResId(resId);
	if (!e || (e->type != kResMusic && e->type != kResSound && e->type != kResFXSound)) {
		debugC(kDebugSound, "Music %08x not found", resId);
		return false;
	}

	if (!_musicFile.isOpen() && !_musicFile.open(GAMEBOT_RESOURCE_FILE))
		return false;

	// The music loops forever, as in the original streamed playback
	Common::SeekableSubReadStream *sub = new Common::SeekableSubReadStream(
		&_musicFile, e->location, e->location + e->size);
	Audio::RewindableAudioStream *stream = Audio::makeADPCMStream(
		sub, DisposeAfterUse::YES, e->size, Audio::kADPCMMS, kSampleRate, 1, kBlockAlign);
	if (!stream) {
		debugC(kDebugSound, "Failed to create ADPCM stream for music %08x", resId);
		return false;
	}
	g_engine->_mixer->playStream(Audio::Mixer::kMusicSoundType, &_musicHandle,
		Audio::makeLoopingAudioStream(stream, 0));
	_currentMusic = resId;
	debugC(kDebugSound, "Playing music %08x (%u bytes)", resId, e->size);
	return true;
}

void SoundManager::stopMusic() {
	g_engine->_mixer->stopHandle(_musicHandle);
	_currentMusic = 0;
}

void SoundManager::stopSFX() {
	stopSound();
	for (uint i = 0; i < _watched.size(); i++) {
		g_engine->_mixer->stopHandle(_watched[i].handle);
	}
	_watched.clear();
}

void SoundManager::stopAll() {
	stopSFX();
	stopMusic();
}

} // End of namespace Gamebot
