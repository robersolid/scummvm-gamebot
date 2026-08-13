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

namespace Gamebot {

const PlainGameDescriptor gamebotGames[] = {
	{ "meteoroloca", "Mortadelo y Filem\xc3\xb3n II: La M\xc3\xa1quina Meteoroloca" },
	{ 0, 0 }
};

const ADGameDescription gameDescriptions[] = {
	// Spanish retail CD release (Zeta Multimedia)
	// Detected by the two small immutable data files; the third one,
	// Resdata.res, is 413935382 bytes and holds all graphics and sounds.
	{
		"meteoroloca",
		nullptr,
		AD_ENTRY2s(
			"Actions.act", "fc6424c2fa8f996e1f0ab7227a478dad", 68500,
			"default.def", "34b8ac7cff3000c0ca99e025749a16bd", 60916
		),
		Common::ES_ESP,
		Common::kPlatformWindows,
		ADGF_UNSTABLE | ADGF_DROPPLATFORM,
		GUIO1(GUIO_NOMIDI)
	},

	AD_TABLE_END_MARKER
};

} // End of namespace Gamebot
