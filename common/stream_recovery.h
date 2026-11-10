// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>
namespace wyrm
{
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
