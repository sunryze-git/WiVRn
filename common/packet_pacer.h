// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace wivrn
{
// A bounded-burst, aggregate video budget, shared by all eyes and transports.
class packet_schedule
{
	int64_t next = 0;
	uint64_t bitrate = 0;

public:
	void set_rate(uint64_t value)
	{
		bitrate = value;
		next = 0;
	}
	int64_t reserve(int64_t now, size_t bytes)
	{
		if (not bitrate)
			return now;
		const auto burst = int64_t(8192ULL * 8 * 1'000'000'000 / bitrate);
		const auto deadline = std::max(now, next - burst);
		next = std::max(now, next) + (bytes * 8ULL * 1'000'000'000 + bitrate - 1) / bitrate;
		return deadline;
	}
};

class packet_pacer
{
	std::mutex mutex;
	std::condition_variable changed;
	packet_schedule schedule;
	uint64_t epoch = 0;

public:
	void set_rate(uint64_t bitrate)
	{
		std::lock_guard lock(mutex);
		schedule.set_rate(bitrate);
		++epoch;
		changed.notify_all();
	}
	void pace(size_t bytes)
	{
		using clock = std::chrono::steady_clock;
		std::unique_lock lock(mutex);
		const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count();
		const auto deadline = clock::time_point(std::chrono::nanoseconds(schedule.reserve(now, bytes)));
		const auto start_epoch = epoch;
		changed.wait_until(lock, deadline, [&] { return epoch != start_epoch; });
	}
};
} // namespace wivrn
