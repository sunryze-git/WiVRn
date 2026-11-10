/*
 * Wyrm VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "wyrm_config.h"

#ifdef __ANDROID__
#include "android/audio.h"
namespace wyrm
{
using audio = ::wyrm::android::audio;
}
#elif WYRM_USE_PIPEWIRE
#include "linux/audio.h"
namespace wyrm
{
using audio = ::wyrm::linux_audio::audio;
}
#else

#include "wyrm_client.h"
#include "wyrm_packets.h"
#include "xr/instance.h"

namespace wyrm
{
class audio
{
public:
	audio(const audio &) = delete;
	audio & operator=(const audio &) = delete;
	audio(const wyrm::to_headset::audio_stream_description &, wyrm_session &, xr::instance &) {}
	~audio() = default;

	void operator()(wyrm::audio_data &&) {}
	void set_mic_state(bool running) {}

	static void get_audio_description(wyrm::from_headset::headset_info_packet & info) {}
};
} // namespace wyrm

#endif
