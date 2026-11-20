#pragma once
#include "decoder/shard_accumulator.h"
#include <memory>
#include <string>

namespace wivrn
{
// Where a decoder delivers its frames: the VR stream scene, or a frame shell desktop receiver
class frame_sink
{
public:
	virtual ~frame_sink() = default;
	virtual void capture_decode(const from_headset::feedback &, bool, int64_t = 0) {}
	virtual void video_frame_lost(shard_accumulator *) {}
    // Desktop receivers recover through the next enqueue; VR scenes also surface the failure immediately.
    virtual void video_stream_failed(uint64_t, const std::string &) {}
	virtual void push_blit_handle(shard_accumulator * decoder, std::shared_ptr<shard_accumulator::blit_handle> handle) = 0;
};
} // namespace wivrn
