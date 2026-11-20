// SPDX-License-Identifier: GPL-3.0-or-later
// Replays controller samples through pose_list and prints the pose predicted a fixed time after each one, for
// scoring predictors against recorded or simulated motion.
// Usage: replay-pose-prediction <ahead_ms> < samples.csv > predictions.csv
// A sample is t_ns,px,py,pz,qw,qx,qy,qz,vx,vy,vz,wx,wy,wz; a prediction is t_ns,px,py,pz,qw,qx,qy,qz for t + ahead.
#include "clock_offset.h"
#include "pose_list.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace wivrn;

int main(int argc, char ** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: " << argv[0] << " <ahead_ms> < samples.csv > predictions.csv\n";
		return 2;
	}
	const XrDuration ahead = std::atof(argv[1]) * 1'000'000;
	pose_list poses(device_id::LEFT_GRIP);
	poses.predict_motion();

	std::string line;
	while (std::getline(std::cin, line))
	{
		long long t;
		float p[3], q[4], v[3], w[3];
		if (std::sscanf(line.c_str(), "%lld,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f", &t, &p[0], &p[1], &p[2], &q[0], &q[1], &q[2], &q[3], &v[0], &v[1], &v[2], &w[0], &w[1], &w[2]) != 14)
			continue;
		from_headset::tracking packet{};
		packet.production_timestamp = packet.timestamp = t;
		packet.device_poses.push_back({
		        .pose = {.orientation = {.x = q[1], .y = q[2], .z = q[3], .w = q[0]}, .position = {p[0], p[1], p[2]}},
		        .linear_velocity = {v[0], v[1], v[2]},
		        .angular_velocity = {w[0], w[1], w[2]},
		        .device = device_id::LEFT_GRIP,
		        .flags = uint8_t(from_headset::pose_flags::orientation_valid | from_headset::pose_flags::position_valid |
		                         from_headset::pose_flags::linear_velocity_valid | from_headset::pose_flags::angular_velocity_valid),
		});
		poses.update_tracking(packet, {.stable = true});
		const auto pose = poses.get_at(t + ahead).second.pose;
		std::printf("%lld,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g\n", t + ahead, pose.position.x, pose.position.y, pose.position.z, pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
	}
}
