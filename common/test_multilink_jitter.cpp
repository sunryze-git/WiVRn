// SPDX-License-Identifier: GPL-3.0-or-later
#include "multilink.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <poll.h>
#include <source_location>
#include <thread>
#include <unistd.h>

using namespace wyrm;
using namespace std::chrono_literals;

static void check(bool condition, std::source_location where = std::source_location::current())
{
	if (not condition)
	{
		std::cerr << where.file_name() << ':' << where.line() << '\n';
		std::abort();
	}
}

template <typename Predicate>
static bool wait_for(Predicate predicate)
{
	const auto until = std::chrono::steady_clock::now() + 3s;
	do
	{
		if (predicate())
			return true;
		std::this_thread::sleep_for(1ms);
	} while (std::chrono::steady_clock::now() < until);
	return false;
}

#include "test_network_relay.h"

int main()
{
	const auto key = multilink::random_key();
	multilink server(true, key, {{"127.0.0.1", "server"}}, {}, false);
	relay link(server.endpoints().front());
	multilink client(false, key, {{"127.0.0.2", "client"}}, {link.endpoint}, false, false);
	check(wait_for([&] {
		const auto stats = server.statistics();
		return stats.size() == 1 and stats.front().active and stats.front().rtt_ms < 5;
	}));
	std::this_thread::sleep_for(300ms);

	// Delay a few probes, then restore a fast path before sending fresh video.
	link.delay_ms = 40;
	check(wait_for([&] { return server.statistics().front().jitter_ms > 10; }));
	link.delay_ms = 0;
	std::this_thread::sleep_for(100ms);
	const auto stats = server.statistics().front();
	check(stats.active and stats.rtt_ms / 2 + 2 * stats.jitter_ms + 1 > 25);

	std::array<bool, 60> received{};
	const auto drain = [&] {
		for (auto packet = client.receive_raw(); not packet.empty(); packet = client.receive_raw())
		{
			const auto bytes = packet.deserialize<std::vector<uint8_t>>();
			check(bytes.size() == 800 and bytes.front() < received.size());
			check(std::ranges::all_of(bytes.begin() + 1, bytes.end(), [](uint8_t value) { return value == 0x35; }));
			received[bytes.front()] = true;
		}
	};
	for (uint8_t i = 0; i < received.size() / 2; ++i)
	{
		std::vector<uint8_t> bytes(800, 0x35);
		bytes.front() = i;
		serialization_packet packet;
		packet.serialize(bytes);
		server.send_video_raw(std::move(packet), std::chrono::steady_clock::now() + 10ms);
		bytes.front() += received.size() / 2;
		serialization_packet tracking;
		tracking.serialize(bytes);
		server.send_raw(std::move(tracking));
		std::this_thread::sleep_for(2ms);
		drain();
	}
	std::this_thread::sleep_for(50ms);
	drain();
	const auto video = std::count(received.begin(), received.begin() + received.size() / 2, true);
	const auto tracking = std::count(received.begin() + received.size() / 2, received.end(), true);
	std::cout << "Fresh packets after jitter spike: video " << video << "/30, tracking " << tracking << "/30\n"
	          << std::flush;
	check(video >= 27 and tracking >= 27);

	const auto expired = server.diagnostics().expired_video;
	serialization_packet late;
	late.serialize(std::vector<uint8_t>(800, 0xff));
	server.send_video_raw(std::move(late), std::chrono::steady_clock::now() - 1ms);
	check(wait_for([&] { return server.diagnostics().expired_video > expired; }));
	check(client.receive_raw().empty());
}
