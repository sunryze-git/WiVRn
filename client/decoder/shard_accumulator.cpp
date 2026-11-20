/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "shard_accumulator.h"
#include "scenes/stream.h"
#include "shard_recovery.h"
#include "wivrn_serialization.h"
#include "xr/instance.h"
#include <cstdlib>

namespace wivrn
{
void shard_accumulator::open_dump(video_codec codec) noexcept
{
	const char * prefix = std::getenv("WIVRN_DUMP_RECEIVED_VIDEO");
	if (not prefix or not *prefix)
		return;
	try
	{
		const char * extension = "raw";
		if (codec == video_codec::h264)
			extension = "h264";
		else if (codec == video_codec::h265)
			extension = "h265";
		else if (codec == video_codec::av1)
			extension = "av1";
		const auto stem = std::string(prefix) + "-stream" + std::to_string(stream_index);
		dump_filename = stem + "." + extension;
		dump_video.open(dump_filename, std::ios::binary | std::ios::app);
		dump_frames.open(dump_filename + ".jsonl", std::ios::app);
		dump_video.seekp(0, std::ios::end);
		const auto offset = dump_video.tellp();
		if (not dump_video or not dump_frames or offset < 0)
		{
			spdlog::warn("Could not open received video dump {}; disabling it", dump_filename);
			close_dump();
			return;
		}
		dump_offset = static_cast<uint64_t>(offset);
		spdlog::info("Dumping received video to {}", dump_filename);
	}
	catch (...)
	{
		close_dump();
	}
}

void shard_accumulator::dump_received(std::span<const std::span<const uint8_t>> payload, const from_headset::feedback & feedback, XrTime target) noexcept
{
	if (not dump_video.is_open())
		return;
	try
	{
		uint64_t size = 0;
		for (const auto part: payload)
		{
			dump_video.write(reinterpret_cast<const char *>(part.data()), part.size());
			size += part.size();
		}
		if (dump_video)
			dump_frames << "{\"frame_index\":" << feedback.frame_index
			            << ",\"generation\":" << feedback.generation
			            << ",\"stream_index\":" << unsigned(feedback.stream_index)
			            << ",\"target_ns\":" << target
			            << ",\"offset\":" << dump_offset << ",\"size\":" << size << "}\n";
		if (not dump_video or not dump_frames)
		{
			spdlog::warn("Writing received video dump {} failed; disabling it", dump_filename);
			close_dump();
			return;
		}
		dump_offset += size;
	}
	catch (...)
	{
		close_dump();
	}
}

void shard_accumulator::close_dump() noexcept
{
	try
	{
		bool failed = false;
		if (dump_video.is_open())
		{
			dump_video.flush();
			dump_video.close();
			failed |= not dump_video;
		}
		if (dump_frames.is_open())
		{
			dump_frames.flush();
			dump_frames.close();
			failed |= not dump_frames;
		}
		if (failed)
			spdlog::warn("Flushing received video dump {} failed", dump_filename);
	}
	catch (...)
	{}
}

shard_accumulator::~shard_accumulator()
{
	close_dump();
}

void shard_accumulator::push_shard(to_headset::video_shard && shard)
{
	const auto index = shard.frame;
	const auto generation = shard.generation;
	const auto previous = shard.previous;
	if (index < next_frame)
		return;
	auto * frame = recovery.add(std::move(shard));
	if (not frame)
		return;
	auto [it, inserted] = feedbacks.try_emplace(index);
	auto & feedback = it->second;
	if (inserted)
	{
		feedback = {.frame_index = index, .stream_index = stream_index,
		            .received_first_packet = instance.now()};
		// A frame sent before this one never arrived: the decoder lacks its references until a keyframe
		if (next_frame != 0 and previous >= next_frame and previous < index and not recovery.frames.contains(previous))
		{
			from_headset::feedback lost{.generation = generation, .frame_index = previous, .stream_index = stream_index};
			send_feedback(lost);
		}
	}
	feedback.generation = generation;
	if (not feedback.received_first_packet)
		feedback.received_first_packet = instance.now();
	if (frame->complete)
	{
		deserialization_packet metadata({}, frame->data.front());
		auto info = metadata.deserialize<to_headset::video_frame_info>();
		std::vector<std::span<const uint8_t>> payload;
		for (size_t i = 0; i < frame->data.size(); ++i)
			payload.push_back(frame->payload(i));
		payload.front() = payload.front().subspan(serialized_size(info));
		feedback.resent = frame->resent();
		feedback.fec_recovered = frame->recovered;
		feedback.received_last_packet = instance.now();
		feedback.sent_to_decoder = feedback.received_last_packet;
		feedback.encode_begin = info.timing_info.encode_begin;
		feedback.encode_end = info.timing_info.encode_end;
		feedback.send_begin = info.timing_info.send_begin;
		feedback.send_end = info.timing_info.send_end;
		dump_received(payload, feedback, info.view_info.display_time);
		decoder_->push_data(payload, index, false);
		decoder_->frame_completed(feedback, info.view_info);
		send_feedback(feedback);
		next_frame = index + 1;
	}
	else
		// Frames never seen were skipped by the server or reported lost above
		next_frame = recovery.frames.begin()->first;

	for (auto old = recovery.frames.begin(); old != recovery.frames.end() and old->first < next_frame;)
	{
		if (auto f = feedbacks.find(old->first); f != feedbacks.end())
		{
			if (not old->second.complete)
				send_feedback(f->second);
			feedbacks.erase(f);
		}
		old = recovery.frames.erase(old);
	}
	for (auto & [number, pending]: recovery.frames)
		if (number < index and not pending.complete)
			request_missing(number, pending);
}

void shard_accumulator::request_missing(uint64_t number, shards::receiver::frame & frame)
{
	auto now = shards::receiver::clock::now();
	if (frame.last_request != shards::receiver::clock::time_point{} and
	    now - frame.last_request < shards::shard_request_interval)
		return;
	auto missing = recovery.missing(frame, shards::max_shard_requests);
	if (missing.empty())
		return;
	if (auto scene = weak_scene.lock())
		scene->send_nack({.stream = stream_index, .generation = feedbacks.at(number).generation,
		                  .frame = number, .shards = std::move(missing)});
}

void shard_accumulator::request_stalled()
{
	const auto now = shards::receiver::clock::now();
	for (auto & [number, frame]: recovery.frames)
		if (const auto deadline = shards::stalled_recovery_deadline(frame); deadline and now >= *deadline)
			request_missing(number, frame);
}

std::chrono::milliseconds shard_accumulator::recovery_timeout(shards::receiver::clock::time_point now, std::chrono::milliseconds maximum) const
{
	for (const auto & [number, frame]: recovery.frames)
		maximum = shards::recovery_poll_timeout(now, shards::stalled_recovery_deadline(frame), maximum);
	return maximum;
}

void shard_accumulator::send_feedback(from_headset::feedback & feedback)
{
	if (auto scene = weak_scene.lock())
		scene->send_feedback(feedback);
}
} // namespace wivrn
