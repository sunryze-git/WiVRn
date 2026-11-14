// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "wyrm_packets.h"
#include <chrono>
namespace wyrm
{
template <typename Range>
bool acknowledge_decoded_reference(Range & references, const from_headset::feedback & feedback)
{
	if (not feedback.received_from_decoder)
		return false;
	for (auto & reference: references)
		if (reference.frame_index == feedback.frame_index)
		{
			reference.acked = true;
			return true;
		}
	return false;
}

using recovery_clock = std::chrono::steady_clock;
inline bool keyframe_retry_due(recovery_clock::time_point now, recovery_clock::time_point sent)
{
	return now - sent >= std::chrono::milliseconds(200);
}
class decoder_recovery_budget
{
	recovery_clock::time_point window;
	unsigned attempts = 0;

public:
	bool retry(recovery_clock::time_point now)
	{
		if (attempts == 0 or now - window > std::chrono::seconds(30))
		{
			window = now;
			attempts = 0;
		}
		return attempts++ < 3;
	}
};
} // namespace wyrm
