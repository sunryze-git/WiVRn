/*
 * Wyrm VR streaming
 * Copyright (C) 2026  Patrick Nicolas <patricknicolas@laposte.net>
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

// Copyright 2019-2024, Collabora, Ltd.
// Copyright 2025-2026, NVIDIA CORPORATION.

#pragma once

#include "util/comp_base.h"
#include "util/u_logging.h"

#include "encoder/encoder_settings.h"
#include "foveation.h"
#include "layer_squasher.h"
#include "pacer.h"
#include "stream_recovery.h"
#include "utils/wyrm_vk_bundle.h"
#include "video_stream_state.h"

#include "main/comp_compositor.h"

#include <array>
#include <atomic>
#include <memory>
#include <shared_mutex>
#include <thread>

namespace wyrm
{

class wyrm_session;
class video_encoder;

class compositor : public comp_base
{
public:
	struct image
	{
		std::atomic<bool> busy = false;
		image_allocation image;
		vk::raii::ImageView view_y;
		vk::raii::ImageView view_cbcr;
		to_headset::video_stream_data_shard::view_info_t view_info{};
		uint64_t frame_index;
	};

private:
	struct timings
	{
		std::array<float, 50> values{};
		int index = 0;
		u_var_timing var{
		        .values = {
		                .data = values.data(),
		                .index_ptr = &index,
		                .length = int(values.size()),
		        },
		        .range = 1000,
		        .dynamic_rescale = true,
		        .unit = "µs",
		};

		void add(float us);
	};

	const u_logging_level log_level;
	timings squasher_times;
	timings foveation_times;
	wyrm_session & session;
	vk_bundle vk;
	vk::raii::CommandPool cmd_pool;
	vk::raii::QueryPool query_pool;

	struct stream_resources;
	std::unique_ptr<stream_resources> video;
	std::unique_ptr<stream_resources> previous_video;
	std::optional<from_headset::headset_info_packet> pending_video_info;
	mutable std::shared_mutex video_mutex;
	video_stream_state video_state;
	std::chrono::steady_clock::time_point video_change_deadline;
	std::atomic<uint32_t> render_size;
	vk::raii::CommandBuffer cmd;
	vk::raii::Semaphore sem;
	uint64_t sem_value = 0;

	std::atomic<float> requested_refresh_rate;
	std::atomic<float> frame_rate;
	wyrm::pacer pacer;

	std::array<XrVector2f, 2> foveation_angles{};

#ifdef __cpp_lib_atomic_lock_free_type_aliases
	using status_type = std::atomic_signed_lock_free;
#else
	using status_type = std::atomic_int8_t;
#endif
	status_type encode_request{-1}; // id of the image to encode
	std::jthread encoder_thread;

	struct
	{
		comp_frame waited{.id = -1};
		comp_frame rendering{.id = -1};
	} frame;

	xrt_result_t begin_session(const xrt_begin_session_info * info)
	{
		return XRT_SUCCESS;
	};
	xrt_result_t end_session()
	{
		return XRT_SUCCESS;
	};

	xrt_result_t predict_frame(int64_t * out_frame_id,
	                           int64_t * out_wake_time_ns,
	                           int64_t * out_predicted_gpu_time_ns,
	                           int64_t * out_predicted_display_time_ns,
	                           int64_t * out_predicted_display_period_ns);

	xrt_result_t mark_frame(int64_t frame_id,
	                        xrt_compositor_frame_point point,
	                        int64_t when_ns);

	xrt_result_t begin_frame(int64_t frame_id)
	{
		return XRT_SUCCESS;
	}
	xrt_result_t discard_frame(int64_t frame_id)
	{
		return XRT_SUCCESS;
	};

	xrt_result_t layer_commit(xrt_graphics_sync_handle_t sync_handle);

	xrt_result_t get_display_refresh_rate(float * out_display_refresh_rate_hz);

public:
	xrt_result_t request_display_refresh_rate(float display_refresh_rate_hz);

	static xrt_result_t get_view_config(xrt_compositor_native *,
	                                    xrt_view_type view_type,
	                                    xrt_view_config * out_view_config);

private:
	void destroy()
	{
		// do nothing, actually owned by wyrm_session object
	}

	int acquire_image();

	void encoder_work(std::stop_token);
	void encode_image(int index);
	void drain_video();
	void restore_previous_stream();
	void replace_video(std::array<encoder_settings, 3>, vk::Extent3D);
	bool rebuild_encoder_pending = false;
	bool decoder_failed = false;
	decoder_recovery_budget decoder_recovery;
	std::optional<uint32_t> requested_bitrate;

	void send_video_stream_description();

public:
	using base_t = xrt_compositor;
	compositor(wyrm_session &);
	~compositor();

	xrt_system_compositor_info sys_info() const;

	int64_t get_frame_duration() const
	{
		return pacer.get_frame_duration();
	}

	float get_requested_refresh_rate() const
	{
		return requested_refresh_rate;
	}

	float get_framerate() const
	{
		return frame_rate;
	}
	void set_framerate(float hz);

	void set_bitrate(uint32_t);
	void update_foveation(const from_headset::foveation &);

	void resume();
	void reconfigure(const from_headset::headset_info_packet &);
	std::optional<from_headset::headset_info_packet> acknowledge_stream(const from_headset::video_stream_ready &);
	void check_stream_timeout();
	void describe_stream();

	std::optional<from_headset::headset_info_packet> on_feedback(const from_headset::feedback &, const clock_offset &);
};

} // namespace wyrm
