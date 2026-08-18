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

#ifndef GAMEBOT_SOUND_H
#define GAMEBOT_SOUND_H

#include "audio/mixer.h"
#include "common/file.h"

namespace Gamebot {

// All sounds of the game are Microsoft ADPCM, mono, 22050 Hz, 4 bit,
// 512-byte blocks (decoded through ACM in the original SoundSys).
// Voices and effects are one-shot blobs; music streams and loops.
class SoundManager {
public:
	~SoundManager();

	// Plays a one-shot sound (voice or effect) by resource id
	bool playSound(uint32 resId, Audio::Mixer::SoundType type = Audio::Mixer::kSpeechSoundType, uint32 objectId = 0);
	// Sounds that finished since the last call; the rules can react
	// to the original evSoundPopEnded event
	void pollFinishedSounds(Common::Array<uint32> &finished);
	// State and control of one specific playing sound; short sounds
	// mix concurrently like the original DirectSound pops
	bool isSoundPlaying(uint32 resId) const;
	void stopSound(uint32 resId);
	void stopSoundByObject(uint32 objectId);
	bool isSoundPlaying() const;
	void stopSound();

	// Starts the looping background music by resource id
	bool playMusic(uint32 resId);
	void stopMusic();
	void stopSFX();
	void stopAll();

private:
	// Music streams from its own file handle so the mixer thread
	// never fights the resource loads over the seek position
	Common::File _musicFile;
	Audio::SoundHandle _soundHandle;
	Audio::SoundHandle _musicHandle;
	// Every started sound is watched until it ends
	struct WatchedSound {
		Audio::SoundHandle handle;
		uint32 resId;
		uint32 objectId;
	};
	Common::Array<WatchedSound> _watched;
	uint32 _currentMusic = 0;
};

} // End of namespace Gamebot

#endif // GAMEBOT_SOUND_H
