// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "shard_stream.h"
#include <algorithm>
#include <optional>

namespace wyrm::shards
{
inline constexpr auto stalled_shard_delay = std::chrono::milliseconds(3);
inline constexpr auto shard_request_interval = std::chrono::milliseconds(8);
inline constexpr uint8_t max_shard_requests = 2;

inline std::optional<receiver::clock::time_point> stalled_recovery_deadline(const receiver::frame & frame)
{
	if (frame.complete)
		return {};
	bool requestable = false;
	for (size_t i = 0; i < frame.data.size(); ++i)
		requestable |= frame.data[i].empty() and frame.requested[i] < max_shard_requests;
	if (not requestable)
		return {};
	auto deadline = frame.last_shard + stalled_shard_delay;
	if (frame.last_request != receiver::clock::time_point{})
		deadline = std::max(deadline, frame.last_request + shard_request_interval);
	return deadline;
}

inline std::chrono::milliseconds recovery_poll_timeout(
        receiver::clock::time_point now,
        std::optional<receiver::clock::time_point> deadline,
        std::chrono::milliseconds maximum)
{
	if (not deadline)
		return maximum;
	return std::clamp(std::chrono::ceil<std::chrono::milliseconds>(*deadline - now), std::chrono::milliseconds::zero(), maximum);
}
} // namespace wyrm::shards
