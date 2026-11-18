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

#include "app_pacer.h"

#include "duration_summary.h"
#include "util/u_debug.h"
#include "util/u_logging.h"
#include "util/u_metrics.h"
#include "util/u_time.h"
#include "util/u_var.h"
#include "utils/method.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <ranges>

// Default value is half the compositor margin
DEBUG_GET_ONCE_FLOAT_OPTION(min_margin_ms, "U_PACING_APP_MIN_MARGIN_MS", 1.5f)

namespace wyrm
{

class app_pacer : public u_pacing_app
{
	pacing_app_factory & parent;
	int64_t session_id;
	int64_t frame_id = 0;
	int64_t compositor_display_time = 0;
	int64_t last_display_time = 0;
	int64_t period = 10'000'000;

	std::mutex mutex; // locks cpu/draw/gpu time
	int64_t cpu_time = 0;
	int64_t gpu_time = 0;
	int64_t compositor_time = 0;

	// With WYRM_LOG_PREDICTION set: the app's frames against their predicted display time, logged every 5 s
	struct timing_stats
	{
		duration_summary compositor_display, wait_to_display, wait_to_submit, wait_to_gpu_done, gpu_done_to_display, display_moved;
	} timing_log; // locked by mutex
	int64_t timing_log_next = 0;

	u_var_draggable_f32 min_margin_ms;

	struct frame
	{
		int64_t frame_id = 0;
		int64_t predicted_wake_up = 0;
		int64_t wake_up = 0;
		int64_t predicted = 0;
		int64_t begin = 0;
		int64_t submit_begin = 0;
		int64_t submit_end = 0;
		int64_t delivered = 0;
		int64_t gpu_done = 0;
		int64_t predicted_display_time = 0;
		int64_t predicted_display_period = 0;
		int64_t display_time = 0;
		bool discarded = false;
	};
	std::array<frame, 16> frames{};
	void log_timing(const frame & frame, int64_t now);

	frame & get_frame(int64_t id)
	{
		assert(id >= 0);
		return frames[id % frames.size()];
	}

	void write_metrics(const frame & frame)
	{
		if (!u_metrics_is_active())
			return;

		struct u_metrics_session_frame umsf = {
		        .session_id = session_id,
		        .frame_id = frame.frame_id,
		        .predicted_frame_time_ns = cpu_time + gpu_time,
		        .predicted_wake_up_time_ns = frame.predicted_wake_up,
		        .predicted_gpu_done_time_ns = frame.predicted_display_time - compositor_time,
		        .predicted_display_time_ns = frame.predicted_display_time,
		        .predicted_display_period_ns = frame.predicted_display_period,
		        .display_time_ns = frame.display_time,
		        .when_predicted_ns = frame.predicted,
		        .when_wait_woke_ns = frame.wake_up,
		        .when_begin_ns = frame.begin,
		        .when_delivered_ns = frame.delivered,
		        .when_gpu_done_ns = frame.gpu_done,
		        .discarded = frame.discarded,
		        .when_submit_begin_ns = frame.submit_begin,
		        .when_submit_end_ns = frame.submit_end,
		};

		u_metrics_write_session_frame(&umsf);
	}

public:
	using base_t = u_pacing_app;

	app_pacer(pacing_app_factory & parent, int64_t session_id) :
	        u_pacing_app{
	                .predict = method_pointer<&app_pacer::predict>,
	                .mark_point = method_pointer<&app_pacer::mark_point>,
	                .mark_discarded = method_pointer<&app_pacer::mark_discarded>,
	                .mark_delivered = method_pointer<&app_pacer::mark_delivered>,
	                .mark_gpu_done = method_pointer<&app_pacer::mark_gpu_done>,
	                .latched = method_pointer<&app_pacer::latched>,
	                .retired = method_pointer<&app_pacer::retired>,
	                .info = method_pointer<&app_pacer::info>,
	                .destroy = method_pointer<&app_pacer::destroy>,
	        },
	        parent(parent),
	        session_id(session_id),
	        min_margin_ms{
	                .val = debug_get_float_option_min_margin_ms(),
	                .step = 0.1,
	                .min = 0.5,
	                .max = 5.0,
	        }
	{
		// U variable tracking.
		u_var_add_root(this, "App timing info", true);
		u_var_add_draggable_f32(this, &min_margin_ms, "Minimum margin(ms)");
	}

	~app_pacer()
	{
		u_var_remove_root(this);
	}

	void predict(int64_t now_ns,
	             int64_t * out_frame_id,
	             int64_t * out_wake_up_time,
	             int64_t * out_predicted_display_time,
	             int64_t * out_predicted_display_period);
	void mark_point(int64_t frame_id, enum u_timing_point point, int64_t when_ns);
	void mark_discarded(int64_t frame_id, int64_t when_ns);
	void mark_delivered(int64_t frame_id, int64_t when_ns, int64_t display_time_ns);
	void mark_gpu_done(int64_t frame_id, int64_t when_ns);
	void latched(int64_t frame_id, int64_t when_ns, int64_t system_frame_id) {}
	void retired(int64_t frame_id, int64_t when_ns) {}
	void info(int64_t predicted_display_time_ns,
	          int64_t predicted_display_period_ns,
	          int64_t extra_ns);

	int64_t get_app_time()
	{
		std::lock_guard lock(mutex);
		return std::max(cpu_time, gpu_time);
	}

	void destroy()
	{
		parent.remove_app(this);
		delete this;
	}
};

void app_pacer::predict(int64_t now_ns,
                        int64_t * out_frame_id,
                        int64_t * out_wake_up_time,
                        int64_t * out_predicted_display_time,
                        int64_t * out_predicted_display_period)
{
	*out_frame_id = ++frame_id;

	if (log_prediction_enabled())
	{
		std::lock_guard lock(mutex);
		timing_log.compositor_display.add(compositor_display_time - now_ns);
	}

	// no need to lock, called in same thread as writes
	auto min_ready = now_ns + cpu_time + gpu_time + compositor_time;
	// The ideal display time: one frame after the last
	last_display_time += period;
	// Sync phase with compositor
	last_display_time = compositor_display_time + period * ((period / 2 + last_display_time - compositor_display_time) / period);

	if (min_ready > last_display_time and min_ready < last_display_time + period)
	{
		// We are limited by app time, don't wait
		*out_wake_up_time = now_ns;
		*out_predicted_display_period = period; // FIXME: may be more than one frame
		while (last_display_time < min_ready)
			last_display_time += period;
		*out_predicted_display_time = last_display_time;
	}
	else
	{
		// Ensure display time is achievable
		while (last_display_time < min_ready)
			last_display_time += period;

		*out_predicted_display_time = last_display_time;
		*out_predicted_display_period = period;
		*out_wake_up_time = last_display_time - (cpu_time + gpu_time + compositor_time + int64_t(U_TIME_1MS_IN_NS * min_margin_ms.val));
	}

	// wake_up is deliberately left at 0: it is filled in by mark_point() when
	// xrWaitFrame actually returns, and mark_gpu_done() relies on it staying 0
	// to tell "app never woke" apart from a real frame.
	get_frame(frame_id) = {
	        .frame_id = frame_id,
	        .predicted_wake_up = *out_wake_up_time,
	        .predicted = now_ns,
	        .predicted_display_time = *out_predicted_display_time,
	        .predicted_display_period = *out_predicted_display_period,
	        .display_time = *out_predicted_display_time,
	        .discarded = false,
	};
}

void app_pacer::mark_point(int64_t frame_id, enum u_timing_point point, int64_t when_ns)
{
	auto & frame = get_frame(frame_id);
	if (frame.frame_id != frame_id)
		return;
	switch (point)
	{
		case U_TIMING_POINT_WAKE_UP:
			frame.wake_up = when_ns;
			break;
		case U_TIMING_POINT_BEGIN:
			frame.begin = when_ns;
			break;
		case U_TIMING_POINT_SUBMIT_BEGIN:
			frame.submit_begin = when_ns;
			break;
		case U_TIMING_POINT_SUBMIT_END:
			frame.submit_end = when_ns;
			break;
	}
}

void app_pacer::mark_delivered(int64_t frame_id, int64_t when_ns, int64_t display_time_ns)
{
	auto & frame = get_frame(frame_id);
	if (frame.frame_id != frame_id)
		return;
	frame.delivered = when_ns;
	frame.display_time = display_time_ns;
}

void app_pacer::mark_discarded(int64_t frame_id, int64_t when_ns)
{
	auto & frame = get_frame(frame_id);
	if (frame.frame_id != frame_id)
		return;
	frame.delivered = when_ns;
	frame.gpu_done = when_ns;
	frame.discarded = true;
	write_metrics(frame);
}

void app_pacer::info(int64_t predicted_display_time_ns,
                     int64_t predicted_display_period_ns,
                     int64_t extra_ns)
{
	compositor_display_time = predicted_display_time_ns;
	period = predicted_display_period_ns;
	compositor_time = std::max<int64_t>(0, extra_ns);
}

int64_t lerp0(int64_t a, int64_t b, double t)
{
	if (a == 0)
		return b;
	return std::lerp(a, b, t);
}

void app_pacer::mark_gpu_done(int64_t frame_id, int64_t when_ns)
{
	auto & frame = get_frame(frame_id);
	if (frame.frame_id != frame_id or not(frame.wake_up and frame.delivered))
		return;

	frame.gpu_done = when_ns;

	std::lock_guard lock(mutex);
	cpu_time = lerp0(cpu_time, std::min(3 * period, frame.delivered - frame.wake_up), 0.1);
	gpu_time = lerp0(gpu_time, std::min(3 * period, when_ns - frame.delivered), 0.1);
	write_metrics(frame);
	if (log_prediction_enabled())
		log_timing(frame, when_ns);
}

void app_pacer::log_timing(const frame & frame, int64_t now)
{
	timing_log.wait_to_display.add(frame.predicted_display_time - frame.wake_up);
	if (frame.submit_end)
		timing_log.wait_to_submit.add(frame.submit_end - frame.wake_up);
	timing_log.wait_to_gpu_done.add(frame.gpu_done - frame.wake_up);
	timing_log.gpu_done_to_display.add(frame.predicted_display_time - frame.gpu_done);
	timing_log.display_moved.add(frame.display_time - frame.predicted_display_time);

	if (timing_log_next == 0)
		timing_log_next = now + 5'000'000'000;
	if (now < timing_log_next)
		return;
	timing_log_next = now + 5'000'000'000;
	U_LOG_I("App %" PRId64 " frames (%" PRId64 " in 5 s): xrWaitFrame returned %s before the predicted display time, "
	        "submitted %s and GPU done %s after xrWaitFrame, GPU done %s before display, display time moved %s at delivery, "
	        "compositor display time %s ahead at prediction; estimates CPU %.1f ms, GPU %.1f ms, compositor %.1f ms, period %.1f ms",
	        session_id,
	        timing_log.wait_to_display.count,
	        timing_log.wait_to_display.str().c_str(),
	        timing_log.wait_to_submit.str().c_str(),
	        timing_log.wait_to_gpu_done.str().c_str(),
	        timing_log.gpu_done_to_display.str().c_str(),
	        timing_log.display_moved.str().c_str(),
	        timing_log.compositor_display.str().c_str(),
	        cpu_time / 1e6,
	        gpu_time / 1e6,
	        compositor_time / 1e6,
	        period / 1e6);
	timing_log = {};
}

pacing_app_factory::pacing_app_factory() :
        u_pacing_app_factory{
                .create = method_pointer<&pacing_app_factory::create>,
                .destroy = method_pointer<&pacing_app_factory::destroy>,
        }
{
}

void pacing_app_factory::remove_app(app_pacer * app)
{
	std::lock_guard lock(mutex);
	std::erase(app_pacers, app);
}

xrt_result_t pacing_app_factory::create(struct u_pacing_app ** out_upa)
{
	std::lock_guard lock(mutex);
	*out_upa = app_pacers.emplace_back(new app_pacer(*this, next_session_id++));
	return XRT_SUCCESS;
}

void pacing_app_factory::destroy()
{
}

int64_t pacing_app_factory::get_frame_time()
{
	std::lock_guard lock(mutex);
	if (app_pacers.empty())
		return 1;
	return std::ranges::max(std::ranges::transform_view(app_pacers, [](auto * pacer) { return pacer->get_app_time(); }));
}

} // namespace wyrm
