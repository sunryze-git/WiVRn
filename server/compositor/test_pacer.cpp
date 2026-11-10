// SPDX-License-Identifier: GPL-3.0-or-later
#include "driver/clock_offset.h"
#include "encoder/idr_handler.h"
#include "os/os_time.h"
#include "pacer.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
static void check(bool valid, std::source_location where = std::source_location::current())
{
	if (not valid)
	{
		std::cerr << where.file_name() << ':' << where.line() << '\n';
		std::abort();
	}
}
int main()
{
	using namespace wivrn;
	default_idr_handler idr;
	check(not idr.should_skip(0, 0));
	check(idr.get_type(0, 0) == default_idr_handler::frame_type::i);
	check(idr.should_skip(1, 0));
	idr.on_feedback({.frame_index = 0, .stream_index = 0, .sent_to_decoder = 1});
	check(not idr.should_skip(1, 0));
	check(idr.get_type(1, 0) == default_idr_handler::frame_type::p);
	idr.set_non_ref(1);
	idr.on_feedback({.frame_index = 1, .stream_index = 0});
	check(idr.get_type(2, 0) == default_idr_handler::frame_type::p);
	idr.on_feedback({.frame_index = 2, .stream_index = 0});
	check(not idr.should_skip(3, 0));
	check(idr.get_type(3, 0) == default_idr_handler::frame_type::i);
	idr.on_feedback({.frame_index = 0, .stream_index = 0, .sent_to_decoder = 1});
	check(idr.should_skip(4, 0)); // stale ACK must not acknowledge keyframe 3
	idr.on_feedback({.frame_index = 3, .stream_index = 0});
	check(not idr.should_skip(5, 0));
	check(idr.get_type(5, 0) == default_idr_handler::frame_type::i);
	idr.reset();
	check(not idr.should_skip(6, 0));
	check(idr.get_type(6, 0) == default_idr_handler::frame_type::i);

	{
		pacer timing(1'000'000'000 / 120);
		int64_t id, wake, present, slop, display;
		timing.predict(id, wake, present, slop, display);
		const auto delayed_id = id, delayed_display = display;
		for (int i = 0; i < 12; ++i)
			timing.predict(id, wake, present, slop, display);
		timing.on_feedback({.frame_index = uint64_t(delayed_id), .stream_index = 1, .received_from_decoder = delayed_display, .blitted = delayed_display, .displayed = delayed_display + 40'000'000}, {.stable = true});
		timing.predict(id, wake, present, slop, display);
		check(display - present > 0); // feedback older than the old eight-frame window
	}

	for (int iteration = 0; iteration < 30; ++iteration)
	{
		pacer timing(1'000'000'000 / 120);
		int64_t id, wake, present, slop, display;
		for (int i = 0; i < 102; ++i)
		{
			timing.predict(id, wake, present, slop, display);
			check(present >= os_monotonic_get_ns());
			check(timing.present_to_info(present).frame_id == id);
			timing.on_feedback({.frame_index = uint64_t(id), .stream_index = 0, .received_from_decoder = present + 8'000'000, .blitted = display + 10'000'000, .displayed = display + 50'000'000},
			                   {.stable = true});
		}
		const auto stale_id = id;
		timing.set_frame_duration(1'000'000'000 / 90);
		timing.on_feedback({.frame_index = uint64_t(stale_id), .stream_index = 1, .received_from_decoder = present + 8'000'000, .blitted = display, .displayed = display + 80'000'000}, {.stable = true});
		timing.predict(id, wake, present, slop, display);
		check(timing.get_frame_duration() == 1'000'000'000 / 90);
		check(display == present); // old phase, latency samples and feedback cleared
		check(display < os_monotonic_get_ns() + 2 * int64_t(timing.get_frame_duration()));
		timing.reset(); // exercise worker shutdown while a computation is pending
	}
	std::cout << "Pacer refresh resets, stale feedback and keyframe recovery passed\n";
}
