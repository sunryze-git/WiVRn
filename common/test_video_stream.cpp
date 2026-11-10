// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_stream_state.h"
#include "wyrm_packets.h"
#include "wyrm_serialization.h"

#include <cstdlib>
#include <iostream>

using namespace wyrm;

static void check(bool condition)
{
	if (not condition)
		std::abort();
}

template <typename T>
static T roundtrip(const T & value)
{
	serialization_packet packet;
	packet.serialize(value);
	std::vector<uint8_t> bytes;
	for (auto span: static_cast<std::vector<std::span<uint8_t>> &>(packet))
		bytes.insert(bytes.end(), span.begin(), span.end());
	auto memory = std::make_shared<uint8_t[]>(bytes.size());
	std::ranges::copy(bytes, memory.get());
	deserialization_packet decoded(memory, std::span(memory.get(), bytes.size()));
	auto result = decoded.deserialize<T>();
	check(decoded.empty());
	return result;
}

int main()
{
	from_headset::headset_info_packet info{};
	info.available_refresh_rates = {72, 90, 120};
	info.current_refresh_rate = 119.88f;
	info.refresh_rate_external = true;
	const auto decoded_info = roundtrip(info);
	check(decoded_info.available_refresh_rates == info.available_refresh_rates);
	check(decoded_info.current_refresh_rate == info.current_refresh_rate and decoded_info.refresh_rate_external);
	video_stream_state stream;
	check(not stream.ready());
	stream.acknowledge(0);
	check(not stream.ready());
	stream.acknowledge(stream.generation());
	check(stream.ready());

	// Repeated changes, delayed acknowledgements and packets from previous
	// streams must never open the gate for a replacement decoder.
	for (int i = 0; i < 1000; ++i)
	{
		const auto old = stream.generation();
		stream.replace();
		check(not stream.ready());
		stream.acknowledge(old);
		check(not stream.ready());
		check(not stream.accepts(old));
		stream.decoded(stream.generation(), 0);
		stream.decoded(stream.generation(), 1);
		check(not stream.confirmed());
		stream.acknowledge(stream.generation());
		check(stream.ready());
		stream.decoded(stream.generation(), 0);
		check(not stream.confirmed());
		stream.decoded(stream.generation(), 2);
		stream.decoded(old, 1);
		check(not stream.confirmed());
		stream.decoded(stream.generation(), 1);
		check(stream.confirmed());
		stream.suspend();
		check(not stream.ready());
		check(not stream.confirmed());
		stream.acknowledge(old);
		check(not stream.ready());
		stream.acknowledge(stream.generation());
		check(stream.ready());
	}

	to_headset::video_stream_description description{
	        .generation = stream.generation(),
	        .width = 1536,
	        .height = 1600,
	        .codec = {h265, h265, h265},
	        .frame_rate = 90,
	        .refresh_rate = 90,
	        .bit_depth = 10,
	};
	check(roundtrip(description) == description);
	from_headset::packets rejected = from_headset::video_stream_ready{
	        .generation = description.generation,
	        .accepted = false,
	        .error = "Decoder allocation failed",
	};
	const auto ready = std::get<from_headset::video_stream_ready>(roundtrip(rejected));
	check(ready.generation == description.generation and not ready.accepted and ready.error == "Decoder allocation failed");

	std::vector<uint8_t> payload(256, 42);
	to_headset::video_stream_data_shard shard{
	        .generation = description.generation,
	        .stream_item_idx = 1,
	        .frame_idx = 9000,
	        .shard_idx = 0,
	        .payload = payload,
	};
	const auto decoded_shard = roundtrip(shard);
	check(stream.accepts(decoded_shard.generation));
	check(decoded_shard.frame_idx == 9000 and std::ranges::equal(decoded_shard.payload, payload));
	stream.replace();
	check(not stream.accepts(decoded_shard.generation));

	from_headset::feedback feedback{.generation = description.generation, .frame_index = 9000, .stream_index = 1};
	check(roundtrip(feedback).generation == description.generation);
	std::cout << "Video stream handoff and protocol tests passed\n";
}
