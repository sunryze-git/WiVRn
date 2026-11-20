// SPDX-License-Identifier: GPL-3.0-or-later
#include "shard_stream.h"
#include "wivrn_serialization.h"
#include <cstdlib>
#include <iostream>
#include <source_location>

using namespace wivrn;
static void check(bool valid, std::source_location where = std::source_location::current())
{
	if (!valid)
	{
		std::cerr << where.file_name() << ':' << where.line() << '\n';
		std::abort();
	}
}
static to_headset::video_shard wire(const to_headset::video_shard & shard)
{
	serialization_packet encoded;
	encoded.serialize(shard);
	std::vector<uint8_t> data;
	for (auto bytes: static_cast<std::vector<std::span<uint8_t>> &>(encoded))
		data.insert(data.end(), bytes.begin(), bytes.end());
	auto memory = std::make_shared<uint8_t[]>(data.size());
	std::ranges::copy(data, memory.get());
	deserialization_packet decoded(memory, std::span(memory.get(), data.size()));
	return decoded.deserialize<to_headset::video_shard>();
}
static std::vector<uint8_t> contents(const shards::receiver::frame & frame)
{
	std::vector<uint8_t> result;
	for (size_t i = 0; i < frame.data_count; ++i)
	{
		auto data = frame.payload(i);
		result.insert(result.end(), data.begin(), data.end());
	}
	return result;
}
int main()
{
	std::vector<uint8_t> data(to_headset::video_shard::max_payload_size * 4 + 37);
	for (size_t i = 0; i < data.size(); ++i)
		data[i] = uint8_t((i * 37 + 19) % 251);
	for (uint32_t stream: {0u, to_headset::video_shard::desktop_streams + 1})
	{
		shards::sender sender;
		shards::receiver receiver;
		auto encoded = sender.frame(stream, 7, 42, data, 40);
		check(encoded.front().parity_count > 0);
		for (auto & shard: encoded)
			if (shard.index != 1)
				check(receiver.add(wire(shard)) != nullptr);
		const auto & frame = receiver.frames.at(42);
		check(frame.complete && frame.recovered > 0);
		check(contents(frame) == data);
		auto invalid = encoded.front();
		invalid.data_count = 0;
		check(receiver.add(std::move(invalid)) == nullptr);

		shards::receiver retransmitted;
		encoded = sender.frame(stream, 7, 43, data, 0);
		for (auto & shard: encoded)
			if (shard.index != 2)
				check(retransmitted.add(wire(shard)) != nullptr);
		auto & incomplete = retransmitted.frames.at(43);
		check(!incomplete.complete);
		auto missing = retransmitted.missing(incomplete, 2);
		check(missing == std::vector<uint16_t>{2});
		from_headset::video_nack request{.stream = stream, .generation = 8, .frame = 43, .shards = missing};
		check(sender.resend(request).empty());
		request.generation = 7;
		auto repair = sender.resend(request);
		check(repair.size() == 1);
		check(retransmitted.add(wire(repair.front())) != nullptr);
		check(incomplete.complete && incomplete.resent());
		check(contents(incomplete) == data);
		for (uint64_t next = 44; next <= 51; ++next)
			sender.frame(stream, 7, next, data, 0);
		check(sender.resend(request).empty());
	}
	std::cout << "VR and desktop wire shards, FEC, retransmission and generation checks passed\n";
}
