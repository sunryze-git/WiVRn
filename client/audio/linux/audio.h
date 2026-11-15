/*
 * Wyrm VR streaming
 * Copyright (C) 2026  galister <galister-dev@pm.me>
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

#include "utils/ring_buffer.h"
#include "audio_link.h"
#include "wyrm_packets.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <string>

struct pw_main_loop;
struct pw_stream;
struct pw_stream_events;
struct pw_thread_loop;

class wyrm_session;

namespace xr
{
class instance;
}

namespace wyrm::linux_audio
{
struct pipewire_deleter
{
	void operator()(pw_main_loop * loop) const noexcept;
	void operator()(pw_thread_loop * loop) const noexcept;
	void operator()(pw_stream * stream) const noexcept;
};

template <typename T>
using pipewire_ptr = std::unique_ptr<T, pipewire_deleter>;

class audio
{
	static void speaker_process(void * userdata);
	static void microphone_process(void * userdata);

	static const pw_stream_events speaker_events;
	static const pw_stream_events microphone_events;

	void build_speaker(const to_headset::audio_stream_description::device & device);
	void build_microphone(const to_headset::audio_stream_description::device & device);
	void microphone_sender();
	void shutdown_pipewire();

	to_headset::audio_stream_description desc;
	wyrm_session & session;
	xr::instance & instance;

	pipewire_ptr<pw_thread_loop> loop;
	pipewire_ptr<pw_stream> speaker;
	pipewire_ptr<pw_stream> microphone;

	utils::ring_buffer<wyrm::audio_data, 100> speaker_samples;
	std::atomic<size_t> speaker_buffer_size_bytes = 0;
	wyrm::audio_data speaker_current;
	bool speaker_primed = false;
	// Over the stream socket: microphone chunks out, speaker chunks back in order (network thread)
	wyrm::audio_link::sender microphone_link;
	wyrm::audio_link::receiver speaker_link;

	utils::ring_buffer<wyrm::audio_data, 100> microphone_samples;
	std::atomic<size_t> microphone_buffer_size_bytes = 0;
	std::mutex microphone_mutex;
	std::condition_variable microphone_cv;
	std::atomic<bool> exiting = false;
	std::atomic<bool> mic_running = false;
	std::thread microphone_thread;

public:
	audio(const audio &) = delete;
	audio & operator=(const audio &) = delete;
	audio(const wyrm::to_headset::audio_stream_description &, wyrm_session &, xr::instance &);
	~audio();

	void operator()(wyrm::audio_data &&);
	void set_mic_state(bool running);

	static void get_audio_description(wyrm::from_headset::headset_info_packet & info);
};
} // namespace wyrm::linux_audio
