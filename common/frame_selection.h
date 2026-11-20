// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <utility>

namespace wivrn
{
template <typename Frame>
bool stereo_frames_match(const std::array<std::shared_ptr<Frame>, 3> & frames)
{
	if (not frames[0] or not frames[1] or frames[0]->feedback.frame_index != frames[1]->feedback.frame_index)
		return false;
	return not frames[0]->view_info.alpha or
	       (frames[2] and frames[2]->feedback.frame_index == frames[0]->feedback.frame_index);
}

template <typename Frame>
std::array<std::shared_ptr<Frame>, 3> select_stereo_frame(
        const std::array<std::span<const std::shared_ptr<Frame>>, 3> & frames,
        const std::array<std::shared_ptr<Frame>, 3> & previous,
        int64_t display_time)
{
	using handle = std::shared_ptr<Frame>;
	std::array<handle, 3> result{};
	uint64_t minimum_frame = 0;
	for (size_t i = 0; i < 2; ++i)
		if (previous[i])
			minimum_frame = std::max(minimum_frame, previous[i]->feedback.frame_index);
	const auto distance = [display_time](const handle & frame) {
		return std::abs(frame->view_info.display_time - display_time);
	};
	const auto find_frame = [&](size_t stream, uint64_t index) -> handle {
		for (const auto & frame: frames[stream])
			if (frame and frame->feedback.frame_index == index)
				return frame;
		if (previous[stream] and previous[stream]->feedback.frame_index == index)
			return previous[stream];
		return {};
	};

	for (const auto & left: frames[0])
	{
		if (not left or left->feedback.frame_index < minimum_frame)
			continue;
		std::array<handle, 3> candidate{left, find_frame(1, left->feedback.frame_index), {}};
		if (left->view_info.alpha)
			candidate[2] = find_frame(2, left->feedback.frame_index);
		if (stereo_frames_match(candidate) and (not result[0] or distance(left) < distance(result[0])))
			result = std::move(candidate);
	}
	if (result[0])
		return result;
	// The rolling decoder buffers can evict the last complete stereo pair during a stall.
	if (stereo_frames_match(previous))
		return previous;

	// Before the first matched pair, keep the independent-eye fallback moving forward.
	for (size_t stream = 0; stream < result.size(); ++stream)
	{
		result[stream] = previous[stream];
		for (const auto & frame: frames[stream])
			if (frame and
			    (not previous[stream] or frame->feedback.frame_index >= previous[stream]->feedback.frame_index) and
			    (not result[stream] or distance(frame) < distance(result[stream])))
				result[stream] = frame;
	}
	if (result[0] and not result[0]->view_info.alpha)
		result[2].reset();
	return result;
}
} // namespace wivrn
