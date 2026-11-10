// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <utility>
namespace wyrm
{
// Half the peripheral tangent-space span. Shift the whole inset at the edges
// so it remains inside the context view and keeps a constant pixel density.
inline std::pair<float, float> inset_angles(float low, float high, float gaze)
{
	const auto lo = std::tan(low), hi = std::tan(high);
	const auto half = (hi - lo) * 0.25f;
	const auto center = std::clamp(std::isfinite(gaze) ? std::tan(std::clamp(gaze, low, high)) : (lo + hi) / 2, lo + half, hi - half);
	return {std::atan(center - half), std::atan(center + half)};
}
} // namespace wyrm
