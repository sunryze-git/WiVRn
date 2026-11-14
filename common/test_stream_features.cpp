// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_timing.h"
#include "nvenc_options.h"
#include "packet_pacer.h"
#include "panel_mode.h"
#include "quad_view.h"
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
int main()
{
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
