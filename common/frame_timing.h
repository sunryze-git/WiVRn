// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace wyrm
{
// Return the first phase-aligned deadline at or after ready, never in the past.
inline int64_t next_frame_deadline(int64_t phase, int64_t ready, int64_t period)
{
	if (ready <= phase)
		return phase;
	return phase + ((ready - phase + period - 1) / period) * period;
}
} // namespace wyrm
