// SPDX-License-Identifier: GPL-3.0-or-later
#include "foveated_sharpening.h"
#include "frame_selection.h"
#include "frame_timing.h"
#include "network_poll.h"
#include "nvenc_options.h"
#include "packet_pacer.h"
#include "panel_mode.h"
#include "quad_view.h"
#include "queue_family_transfer.h"
#include "shard_recovery.h"
#include "stream_recovery.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
using namespace wyrm;
static void check(bool valid, std::source_location where = std::source_location::current())
{
	if (not valid)
	{
		std::cerr << where.file_name() << ':' << where.line() << '\n';
		std::abort();
	}
}
static void rejects(std::map<std::string, std::string> settings)
{
	bool rejected = false;
	try
	{
		nvenc_options options(settings);
	}
	catch (const std::runtime_error &)
	{
		rejected = true;
	}
	check(rejected);
}

static void test_frame_selection()
{
	struct frame
	{
		from_headset::feedback feedback;
		struct
		{
			int64_t display_time;
			bool alpha;
		} view_info;
	};
	using handle = std::shared_ptr<frame>;
	std::array<std::array<handle, 3>, 3> decoded{};
	std::array<handle, 3> selected{};
	const auto make_frame = [](uint64_t index, int64_t time, bool alpha = false) {
		return std::make_shared<frame>(frame{{.frame_index = index}, {time, alpha}});
	};
	const auto select = [&](int64_t time) {
		std::array<std::span<const handle>, 3> available;
		for (size_t i = 0; i < decoded.size(); ++i)
			available[i] = decoded[i];
		selected = select_stereo_frame(available, selected, time);
	};
	decoded[0][0] = make_frame(30020, 27'776'990'000);
	decoded[1][0] = make_frame(30020, 27'776'990'000);
	select(27'776'990'000);
	check(stereo_frames_match(selected) and selected[0]->feedback.frame_index == 30020);
	const auto held = selected;

	// Captured IDs regressed from 30020 to 30019 while newer eyes no longer matched.
	decoded[0] = {make_frame(30019, 27'770'000'000), make_frame(30021, 27'784'000'000), {}};
	decoded[1] = {make_frame(30019, 27'770'000'000), make_frame(30022, 27'791'000'000), {}};
	select(30'610'550'000);
	check(selected == held);
	decoded[0] = {make_frame(31000, 35'000'000'000), {}, {}};
	decoded[1] = {make_frame(31001, 35'007'000'000), {}, {}};
	select(39'193'810'000);
	check(selected == held);
	decoded[0][1] = make_frame(31631, 39'200'760'000);
	decoded[1][1] = make_frame(31631, 39'200'760'000);
	select(39'200'760'000);
	check(stereo_frames_match(selected) and selected[0]->feedback.frame_index == 31631);

	// A display-time adjustment must not reverse the selected frame.
	decoded[0][2] = make_frame(31630, 39'190'000'000);
	decoded[1][2] = make_frame(31630, 39'190'000'000);
	select(39'190'000'000);
	check(selected[0]->feedback.frame_index == 31631);

	// Stream replacement clears the retained frames and permits a fresh frame counter.
	selected = {};
	decoded = {};
	decoded[0][0] = make_frame(100, 100);
	decoded[1][0] = make_frame(101, 101);
	select(101);
	check(selected[0]->feedback.frame_index == 100 and selected[1]->feedback.frame_index == 101);
	decoded[0][1] = make_frame(99, 99);
	decoded[1][1] = make_frame(99, 99);
	select(99);
	check(selected[0]->feedback.frame_index == 100 and selected[1]->feedback.frame_index == 101);
	decoded[0][2] = make_frame(102, 102);
	decoded[1][2] = make_frame(102, 102);
	select(102);
	check(stereo_frames_match(selected) and selected[0]->feedback.frame_index == 102);

	decoded = {};
	decoded[0][0] = make_frame(103, 103, true);
	decoded[1][0] = make_frame(103, 103, true);
	decoded[2][0] = make_frame(102, 102, true);
	select(103);
	check(selected[0]->feedback.frame_index == 102 and not selected[2]);
	decoded[2][1] = make_frame(103, 103, true);
	select(103);
	check(stereo_frames_match(selected) and selected[2]->feedback.frame_index == 103);
	decoded[0][1] = make_frame(104, 104);
	decoded[1][1] = make_frame(104, 104);
	select(104);
	check(stereo_frames_match(selected) and selected[0]->feedback.frame_index == 104 and not selected[2]);

	decoded = {};
	decoded[0] = {make_frame(104, 104), make_frame(105, 200), {}};
	decoded[1] = {make_frame(104, 104), make_frame(105, 200), {}};
	select(110);
	check(selected[0]->feedback.frame_index == 104);
	// Early decoded pairs must advance after the preceding pair leaves the rolling buffer.
	decoded[0][0].reset();
	decoded[1][0].reset();
	select(110);
	check(stereo_frames_match(selected) and selected[0]->feedback.frame_index == 105);
	select(190);
	check(selected[0]->feedback.frame_index == 105);
	decoded[0] = {make_frame(106, 300), {}, {}};
	decoded[1] = {make_frame(106, 300), {}, {}};
	select(190);
	check(stereo_frames_match(selected) and selected[0]->feedback.frame_index == 106);
}

static void test_shard_recovery_deadline()
{
	using namespace std::chrono_literals;
	const shards::receiver::clock::time_point start(1s);
	std::optional<std::variant<int>> pending = 1;
	shards::receiver::frame queued_frame;
	auto timeout = dispatch_pending_packets(
	        [&] { return std::exchange(pending, std::nullopt); },
	        [&](int) {
		        queued_frame.data = {{1}, {}};
		        queued_frame.requested = {0, 0};
		        queued_frame.last_shard = start;
	        },
	        20ms);
	check(timeout == 0ms);
	check(shards::recovery_poll_timeout(start + timeout, shards::stalled_recovery_deadline(queued_frame), 20ms) == 3ms);
	check(dispatch_pending_packets([&] { return pending; }, [](int) {}, 20ms) == 20ms);
	shards::receiver::frame frame{
	        .data = {{1}, {}, {}},
	        .requested = {0, 0, 0},
	        .last_shard = start,
	};
	check(shards::stalled_recovery_deadline(frame) == start + 3ms);
	auto now = start;
	now += shards::recovery_poll_timeout(now, shards::stalled_recovery_deadline(frame), 20ms);
	check(now == start + 3ms);
	check(shards::recovery_poll_timeout(now, shards::stalled_recovery_deadline(frame), 20ms) == 0ms);
	frame.requested = {0, 1, 1};
	frame.last_request = now;
	check(shards::stalled_recovery_deadline(frame) == start + 11ms);
	check(shards::recovery_poll_timeout(now, shards::stalled_recovery_deadline(frame), 20ms) == 8ms);
	check(shards::recovery_poll_timeout(now + 7ms + 1us, shards::stalled_recovery_deadline(frame), 20ms) == 1ms);
	now += shards::recovery_poll_timeout(now, shards::stalled_recovery_deadline(frame), 20ms);
	check(now == start + 11ms);
	frame.requested = {0, 2, 2};
	frame.last_request = now;
	check(not shards::stalled_recovery_deadline(frame));
	check(shards::recovery_poll_timeout(now, shards::stalled_recovery_deadline(frame), 20ms) == 20ms);
	frame.requested[2] = 1;
	frame.last_shard = now + 7ms;
	check(shards::stalled_recovery_deadline(frame) == now + 10ms);
	frame.data[2] = {2};
	check(not shards::stalled_recovery_deadline(frame));
	frame.requested[1] = 0;
	frame.complete = true;
	check(not shards::stalled_recovery_deadline(frame));
}

int main()
{
	check(parse_sharpening_strength("0.25") == 0.25f);
	check(parse_sharpening_strength("1") == 1);
	for (auto value: {"", "nan", "inf", "-1", "1.1", "0.25junk"})
		check(parse_sharpening_strength(value) == 0);
	const std::array<uint16_t, 7> symmetric{10, 20, 40, 60, 40, 20, 10};
	check(sharpening_axis_bounds(symmetric, 200) == std::array<float, 4>{30, 70, 130, 170});
	const std::array<uint16_t, 3> shifted{5, 30, 10};
	check(sharpening_axis_bounds(shifted, 45) == std::array<float, 4>{0, 5, 35, 45});
	const std::array<uint16_t, 1> uniform{45};
	check(sharpening_axis_bounds(uniform, 45) == std::array<float, 4>{0, 0, 45, 45});
	check(not sharpening_axis_bounds(symmetric, 199));
	check(not sharpening_axis_bounds({}, 0));
	check(not sharpening_axis_bounds(std::span(symmetric).first(6), 190));
	const std::array<uint16_t, 3> zero_native{5, 0, 5};
	check(not sharpening_axis_bounds(zero_native, 10));
	const std::array<uint16_t, 5> missing_fade{10, 0, 20, 0, 10};
	check(not sharpening_axis_bounds(missing_fade, 40));
	const auto region = make_sharpening_region(symmetric, shifted, 200, 45);
	check(region.extent == std::array<int32_t, 2>{200, 45});
	check(region.inner == std::array<float, 4>{70, 5, 130, 35});
	check(region.outer == std::array<float, 4>{30, 0, 170, 45});
	test_frame_selection();
	test_shard_recovery_deadline();
	check(image_queue_transfer_is_implicit(2, 2));
	check(image_queue_transfer_is_implicit(UINT32_MAX, 2));
	check(image_queue_transfer_is_implicit(2, UINT32_MAX));
	check(not image_queue_transfer_is_implicit(0, 2));
	check(not image_queue_transfer_is_implicit(0, 2, false, 1u << 2));
	check(not image_queue_transfer_is_implicit(0, 2, true, 1u << 1));
	check(image_queue_transfer_is_implicit(0, 2, true, 1u << 2));
	check(not image_queue_transfer_is_implicit(2, 0, true, 1u << 2));
	check(not image_queue_transfer_is_implicit(0, 32, true, UINT32_MAX));
	{
		struct reference
		{
			uint64_t frame_index;
			bool acked;
		};
		std::array<reference, 2> references{{{41, true}, {42, false}}};
		from_headset::feedback queued{.frame_index = 42, .sent_to_decoder = 100};
		check(not acknowledge_decoded_reference(references, queued));
		check(references[0].acked and not references[1].acked);
		auto decoded = queued;
		decoded.received_from_decoder = 200;
		check(acknowledge_decoded_reference(references, decoded));
		check(references[1].acked);
		references[1] = {43, false};
		check(not acknowledge_decoded_reference(references, decoded));
		check(not references[1].acked);
		queued.frame_index = 43;
		check(not acknowledge_decoded_reference(references, queued));
		check(not references[1].acked);
	}
	const recovery_clock::time_point start{};
	check(not keyframe_retry_due(start + std::chrono::milliseconds(199), start));
	check(keyframe_retry_due(start + std::chrono::milliseconds(200), start));
	check(keyframe_retry_due(start + std::chrono::seconds(1), start));
	decoder_recovery_budget budget;
	for (int i = 0; i < 3; ++i)
		check(budget.retry(start));
	check(not budget.retry(start));
	check(not budget.retry(start + std::chrono::seconds(30)));
	check(budget.retry(start + std::chrono::seconds(31)));

	// Every supported rate/divider: deadlines remain phase aligned and future
	// facing, even after missing many frames or changing the physical mode.
	for (int rate: {72, 80, 90, 120, 144})
		for (int divider: {1, 2, 3})
		{
			const int64_t period = 1'000'000'000LL * divider / rate;
			for (int64_t ready = 0; ready < 100 * period; ready += 79001)
			{
				const auto deadline = next_frame_deadline(137, ready, period);
				check(deadline >= ready and deadline - ready < period);
				check((deadline - 137) % period == 0);
			}
		}
	packet_schedule schedule;
	schedule.set_rate(500'000'000);
	int64_t previous = 0;
	for (int i = 0; i < 1000; ++i)
	{
		const auto when = schedule.reserve(0, 1400);
		check(when >= previous);
		check(when >= (int64_t(i) * 1400 - 8192) * 8 * 1'000'000'000 / 500'000'000);
		previous = when;
	}
	check(previous < 23'000'000 and previous > 22'000'000);
	check(schedule.reserve(1'000'000'000, 1400) == 1'000'000'000);
	schedule.set_rate(100'000'000);
	check(schedule.reserve(0, 1400) == 0);
	schedule.set_rate(0);
	check(schedule.reserve(17, 1'000'000) == 17);
	const auto panel = parse_panel_mode("2*2160x2160_119.88");
	check(panel and panel->width == 2160 and panel->height == 2160 and std::abs(panel->refresh_rate - 119.88f) < 0.01f);
	for (auto value: {"2160x2160", "2*0x2160_120", "2*2160x2160_nan", "2*2160x2160_120x", "2*2160x2160_0"})
		check(not parse_panel_mode(value));
	// Inner FOV stays completely inside the peripheral FOV, including gaze at
	// the panel edges. Width remains half in tangent space for both eyes.
	for (float eye_offset: {-0.1f, 0.1f})
		for (float gaze = -3.0f; gaze <= 3.0f; gaze += 0.01f)
		{
			const float low = -0.9f + eye_offset, high = 0.9f + eye_offset;
			auto [left, right] = inset_angles(low, high, gaze);
			check(left >= low - 0.00001f and right <= high + 0.00001f and left < right);
			check(std::abs((std::tan(right) - std::tan(left)) / (std::tan(high) - std::tan(low)) - 0.5f) < 0.0001f);
		}
	nvenc_options defaults;
	check(defaults.preset == 4 and defaults.aq and not defaults.temporal_aq);
	nvenc_options quality({{"preset", "P6"}, {"multipass", "full"}, {"temporal-aq", "true"}, {"vbv-frames", "3"}, {"min-qp", "10"}, {"max-qp", "35"}});
	check(quality.preset == 6 and quality.multipass == nvenc_options::passes::full and quality.temporal_aq and quality.max_qp == 35);
	for (int preset = 1; preset <= 7; ++preset)
		check(nvenc_options({{"preset", "p" + std::to_string(preset)}}).preset == unsigned(preset));
	rejects({{"preset", "P8"}});
	rejects({{"preset", "P4junk"}});
	rejects({{"vbv-frames", "nan"}});
	rejects({{"qp", "20.5"}});
	rejects({{"aq-strength", "16"}});
	rejects({{"min-qp", "40"}, {"max-qp", "20"}});
	rejects({{"vbv-frames", "1"}, {"vbv-initial-frames", "2"}});
	rejects({{"intra-refresh-period", "10"}, {"intra-refresh-count", "20"}});
	rejects({{"lookahead", "4"}});
	rejects({{"b-frames", "1"}});
	rejects({{"typo", "true"}});
	std::cout << "Stream timing, packet pacing, panel modes, quad views and NVENC options passed\n";
}
