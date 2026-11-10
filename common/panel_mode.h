// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <charconv>
#include <cmath>
#include <optional>
#include <string_view>
namespace wivrn
{
struct panel_mode
{
	unsigned width, height;
	float refresh_rate;
};
inline std::optional<panel_mode> parse_panel_mode(std::string_view mode)
{
	if (not mode.starts_with("2*"))
		return {};
	mode.remove_prefix(2);
	panel_mode result{};
	auto [x, error] = std::from_chars(mode.data(), mode.data() + mode.size(), result.width);
	if (error != std::errc{} or x == mode.data() + mode.size() or *x != 'x')
		return {};
	auto [separator, height_error] = std::from_chars(x + 1, mode.data() + mode.size(), result.height);
	if (height_error != std::errc{} or separator == mode.data() + mode.size() or *separator != '_')
		return {};
	auto [end, rate_error] = std::from_chars(separator + 1, mode.data() + mode.size(), result.refresh_rate);
	if (rate_error != std::errc{} or end != mode.data() + mode.size() or not std::isfinite(result.refresh_rate) or
	    result.width == 0 or result.width > 16384 or result.height == 0 or result.height > 16384 or
	    result.refresh_rate < 30 or result.refresh_rate > 300)
		return {};
	return result;
}
} // namespace wivrn
