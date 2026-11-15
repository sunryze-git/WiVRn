// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <optional>
#include <span>
#include <string_view>

namespace wyrm
{
inline float parse_sharpening_strength(std::string_view text)
{
	float strength = 0;
	auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), strength);
	return error == std::errc{} and end == text.data() + text.size() and std::isfinite(strength) and
	                       strength >= 0 and strength <= 1
	               ? strength
	               : 0;
}

inline std::optional<std::array<float, 4>> sharpening_axis_bounds(std::span<const uint16_t> counts, uint32_t encoded_size)
{
	if (counts.empty() or counts.size() % 2 == 0 or
	    std::accumulate(counts.begin(), counts.end(), uint32_t(0)) != encoded_size)
		return {};
	const size_t middle = counts.size() / 2;
	if (counts[middle] == 0)
		return {};
	const float start = std::accumulate(counts.begin(), counts.begin() + middle, uint32_t(0));
	const float end = start + counts[middle];
	if (middle and ((start > 0 and counts[middle - 1] == 0) or (end < encoded_size and counts[middle + 1] == 0)))
		return {};
	// Fade through the adjacent 2:1 bands; coarser regions receive no sharpening.
	return std::array{middle ? start - counts[middle - 1] : start, start, end, middle ? end + counts[middle + 1] : end};
}

struct sharpening_region
{
	std::array<int32_t, 2> extent{};
	std::array<float, 4> inner{};
	std::array<float, 4> outer{};
};

inline sharpening_region make_sharpening_region(
        std::span<const uint16_t> x,
        std::span<const uint16_t> y,
        uint32_t width,
        uint32_t height)
{
	const auto bx = sharpening_axis_bounds(x, width), by = sharpening_axis_bounds(y, height);
	if (not bx or not by)
		return {};
	return {{int32_t(width), int32_t(height)}, {(*bx)[1], (*by)[1], (*bx)[2], (*by)[2]}, {(*bx)[0], (*by)[0], (*bx)[3], (*by)[3]}};
}
} // namespace wyrm
