// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace wyrm
{
// Protected by the compositor's video mutex. A replacement stream cannot send
// frames until its decoder has been installed, including after reconnecting.
class video_stream_state
{
	uint64_t generation_ = 1;
	bool ready_ = false;
	uint8_t decoded_eyes = 0;

public:
	uint64_t generation() const
	{
		return generation_;
	}
	bool ready() const
	{
		return ready_;
	}
	void suspend()
	{
		ready_ = false;
		decoded_eyes = 0;
	}
	void replace()
	{
		++generation_;
		suspend();
	}
	void acknowledge(uint64_t generation)
	{
		if (generation == generation_)
			ready_ = true;
	}
	bool accepts(uint64_t generation) const
	{
		return generation == generation_;
	}
	void decoded(uint64_t generation, uint8_t eye)
	{
		if (ready_ and accepts(generation) and eye < 2)
			decoded_eyes |= 1 << eye;
	}
	bool confirmed() const
	{
		return decoded_eyes == 3;
	}
};
} // namespace wyrm
