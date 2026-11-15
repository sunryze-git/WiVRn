// SPDX-License-Identifier: GPL-3.0-or-later
#include "driver/eye_osc.h"
#include "model/postprocess/eye_post.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <poll.h>
#include <source_location>
#include <unistd.h>

static void check(bool value, std::source_location where = std::source_location::current())
{
	if (not value)
	{
		std::cerr << where.file_name() << ':' << where.line() << '\n';
		std::exit(1);
	}
}

static void calibration()
{
	babble::eye::OpennessCalibrator::Params params;
	params.skip = 0;
	params.warmup = 4;
	params.every = 1000;
	params.closed_seed = 0.3;
	params.trough_window = 2;
	babble::eye::OpennessCalibrator calibrator(params, std::nullopt);
	for (int i = 0; i < 8; ++i)
		calibrator.update(1.0, 0.01);
	const auto close = [&](double bottom) {
		for (int i = 0; i < 4; ++i)
			calibrator.update(bottom, 0.01);
		for (int i = 0; i < 3; ++i)
			calibrator.update(1.0, 0.01);
	};
	close(0.05);
	close(0.4);
	close(0.4);
	// Ordinary closes must still reach zero after one unusually deep squeeze.
	check(calibrator.update(0.4, 0.01).lid < 1e-6);
	check(std::abs(calibrator.update(1.0, 0.01).lid - 1.0) < 1e-6);

	params.closed_seed = 0;
	babble::eye::OpennessCalibrator uncalibrated(params, std::nullopt);
	for (int i = 0; i < 8; ++i)
		uncalibrated.update(1.0, 0.01);
	check(std::abs(uncalibrated.update(0.4, 0.01).lid - 0.4) < 1e-6);
}

static void native_osc()
{
	int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	check(fd >= 0);
	sockaddr_in address{.sin_family = AF_INET, .sin_port = 0, .sin_addr = {.s_addr = htonl(INADDR_LOOPBACK)}};
	check(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
	socklen_t size = sizeof(address);
	check(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size) == 0);
	wyrm::eye_osc sender("127.0.0.1:" + std::to_string(ntohs(address.sin_port)));
	const auto encode = [&](float left, float right) {
		sender.send({.dilation = {left, right}});
		pollfd ready{.fd = fd, .events = POLLIN};
		check(poll(&ready, 1, 1000) == 1);
		std::array<uint8_t, 256> packet{};
		check(recv(fd, packet.data(), packet.size(), 0) == 132);
		check(std::string_view(reinterpret_cast<char *>(packet.data())) == "/tracking/face/v1");
		check(packet[20] == ',' and packet[21] == 'b');
		check(packet[24] == 0 and packet[25] == 0 and packet[26] == 0 and packet[27] == 102);
		std::array<uint8_t, 102> blob{};
		std::copy_n(packet.begin() + 28, blob.size(), blob.begin());
		return blob;
	};
	// Babble v1.3 maps 0..1 to 2..8 mm, then to a signed weight around 5 mm.
	check(encode(0.5, 0.5) == std::array<uint8_t, 102>{});
	auto blob = encode(0.0, 1.0);
	check(blob[10] == 153 and blob[11] == 0 and blob[12] == 0 and blob[13] == 153);
	blob = encode(0.75, 2.0);
	check(blob[10] == 153 and blob[11] == 76 and blob[12] == 0 and blob[13] == 0);
	blob = encode(-1.0, 0.25);
	check(blob[10] == 0 and blob[11] == 0 and blob[12] == 76 and blob[13] == 0);
	sender.send({.dilation = {-1, -1}});
	pollfd ready{.fd = fd, .events = POLLIN};
	check(poll(&ready, 1, 0) == 0);
	close(fd);
}

int main(int argc, char ** argv)
{
	const std::string_view mode = argc == 2 ? argv[1] : "";
	check(argc <= 2 and (mode.empty() or mode == "--calibration" or mode == "--osc"));
	if (mode.empty() or mode == "--calibration")
		calibration();
	if (mode.empty() or mode == "--osc")
		native_osc();
}
