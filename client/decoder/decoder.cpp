/*
 * Wyrm VR streaming
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "decoder.h"

#ifdef __ANDROID__
#include "decoder/android/android_decoder.h"
#elif WYRM_USE_V4L2
#include "decoder/v4l2/v4l2_decoder.h"
#include "scenes/stream.h"
#else
#include "decoder/ffmpeg/ffmpeg_decoder.h"
#endif
#include "decoder/raw_decoder.h"
#include <algorithm>

wyrm::decoder::~decoder() = default;

bool wyrm::decoder::has_sps(std::span<const uint8_t> data, video_codec codec)
{
	for (size_t i = 0; i + 3 < data.size(); ++i)
		if (data[i] == 0 and data[i + 1] == 0 and data[i + 2] == 1 and
		    (codec == video_codec::h265 ? ((data[i + 3] >> 1) & 0x3f) == 33 : (data[i + 3] & 0x1f) == 7))
			return true;
	return false;
}

std::shared_ptr<wyrm::decoder> wyrm::decoder::make(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice & phys_dev,
        uint32_t vk_queue_family_index,
        const wyrm::to_headset::video_stream_description & description,
        uint8_t stream_index,
        std::weak_ptr<scenes::stream> scene,
        shard_accumulator * acc)
{
	switch (description.codec[stream_index])
	{
		case h264:
		case h265:
		case av1:
#ifdef __ANDROID__
			return std::make_shared<wyrm::android::decoder>(
			        device,
			        phys_dev,
			        description,
			        stream_index,
			        scene,
			        acc);
#elif WYRM_USE_V4L2
			return std::make_shared<wyrm::v4l2::decoder>(
			        device, phys_dev, vk_queue_family_index, description, stream_index, scene, acc);
#else
			return std::make_shared<wyrm::ffmpeg::decoder>(
			        device, phys_dev, description, stream_index, scene, acc);
#endif
		case raw:
			return std::make_shared<wyrm::raw_decoder>(
			        device,
			        phys_dev,
			        vk_queue_family_index,
			        description,
			        stream_index,
			        scene,
			        acc);
	}
	__builtin_unreachable();
}

static std::vector<wyrm::video_codec_capability> supported_codecs_()
{
	std::vector<wyrm::video_codec> codecs;
#ifdef __ANDROID__
	wyrm::android::decoder::supported_codecs(codecs);
#elif WYRM_USE_V4L2
	wyrm::v4l2::decoder::supported_codecs(codecs);
#else
	wyrm::ffmpeg::decoder::supported_codecs(codecs);
#endif
	codecs.push_back(wyrm::video_codec::raw);

	std::vector<wyrm::video_codec_capability> res;
	res.reserve(codecs.size());
	for (auto codec: codecs)
		res.push_back({.codec = codec, .supports_10bit = wyrm::decoder::supports_10bit(codec)});
	return res;
}

const std::vector<wyrm::video_codec_capability> & wyrm::decoder::supported_codecs()
{
	static std::vector<wyrm::video_codec_capability> res = supported_codecs_();
	return res;
}

bool wyrm::decoder::supports_codec(wyrm::video_codec codec)
{
	return std::ranges::any_of(supported_codecs(), [codec](const auto & capability) { return capability.codec == codec; });
}

bool wyrm::decoder::supports_10bit(wyrm::video_codec codec)
{
#ifdef __ANDROID__
	return codec == wyrm::video_codec::h265 or codec == wyrm::video_codec::av1;
#elif WYRM_USE_V4L2
	switch (codec)
	{
		case wyrm::video_codec::h265: {
			static const bool supported = wyrm::v4l2::decoder::supports_10bit(codec);
			return supported;
		}
		case wyrm::video_codec::av1: {
			static const bool supported = wyrm::v4l2::decoder::supports_10bit(codec);
			return supported;
		}
		case wyrm::video_codec::h264:
		case wyrm::video_codec::raw:
			return false;
	}
	__builtin_unreachable();
#else
	// ffmpeg
	return codec == wyrm::video_codec::h265 or codec == wyrm::video_codec::av1;
#endif
}
