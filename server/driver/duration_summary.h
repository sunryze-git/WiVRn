/*
 * Wyrm VR streaming
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <limits>
#include <string>

namespace wyrm
{
// WYRM_LOG_PREDICTION: periodic summaries of display time and pose prediction
inline bool log_prediction_enabled()
{
	static const bool enabled = std::getenv("WYRM_LOG_PREDICTION");
	return enabled;
}

// Mean and range of durations in nanoseconds, printed in milliseconds
struct duration_summary
{
	int64_t count = 0;
	int64_t sum = 0;
	int64_t min = std::numeric_limits<int64_t>::max();
	int64_t max = std::numeric_limits<int64_t>::lowest();

	void add(int64_t ns)
	{
		count++;
		sum += ns;
		min = std::min(min, ns);
		max = std::max(max, ns);
	}

	std::string str() const
	{
		if (count == 0)
			return "n/a";
		return std::format("{:.1f} ms ({:.1f} to {:.1f})", sum / 1e6 / count, min / 1e6, max / 1e6);
	}
};
} // namespace wyrm
