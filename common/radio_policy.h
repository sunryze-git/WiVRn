// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <span>
#include <string>

namespace wyrm
{
struct radio_candidate
{
	std::string interface;
	// Expected delivery time including loss, lower is better
	double score_ms;
};

// Interface that carries traffic on a radio shared by several interfaces. The current choice is
// kept until it has no active path or another interface is better by more than margin_ms.
inline std::string preferred_interface(const std::string & current, std::span<const radio_candidate> candidates, double margin_ms = 2)
{
	if (candidates.empty())
		return {};
	const auto best = std::ranges::min_element(candidates, {}, &radio_candidate::score_ms);
	const auto kept = std::ranges::find(candidates, current, &radio_candidate::interface);
	if (kept != candidates.end() and kept->score_ms <= best->score_ms + margin_ms)
		return current;
	return best->interface;
}
} // namespace wyrm
