/*
 * WiVRn VR streaming
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

// xlib sucks
#ifdef Success
#undef Success
#endif
#ifdef Complex
#undef Complex
#endif

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <array>
#include <deque>
#include <optional>
#include <vector>

#include "openxr/openxr.h"

namespace wivrn
{

// Predicts a controller pose past its newest sample: the reported velocity carried over the delay, so a change in
// the motion shows at once, plus a correction from memory. The memory finds where the last 125 ms of motion happened
// before within 8 s and averages, over the closest matches, how far the real motion then ended up from where the
// velocity pointed: short while a swing brakes, further while it speeds up, off to the side on an arc.
class motion_predictor
{
public:
	struct pose
	{
		Eigen::Vector3d position = Eigen::Vector3d::Zero();
		Eigen::Quaterniond orientation = Eigen::Quaterniond::Identity();
		Eigen::Vector3d linear_velocity = Eigen::Vector3d::Zero();
		Eigen::Vector3d angular_velocity = Eigen::Vector3d::Zero();
	};

	void add_sample(XrTime timestamp, const pose & sample);
	void reset();
	std::optional<XrTime> newest() const;

	// The pose at a time after the newest sample
	std::optional<pose> predict(XrTime at);

private:
	static constexpr int reach = 13; // steps a prediction may look ahead

	struct steps
	{
		Eigen::Vector3d rotation = Eigen::Vector3d::Zero();
		Eigen::Vector3d position = Eigen::Vector3d::Zero();
	};

	struct sample
	{
		XrTime timestamp;
		pose value;
	};

	std::deque<sample> samples;

	// Rebuilt when a sample arrived since the last prediction
	bool stale = true;
	double moving = 0;                       // 0 at rest, where only the reported velocity is used
	std::array<steps, reach + 1> correction; // what the memory adds after each number of steps

	void rebuild();
	steps step(double ahead) const;
};

} // namespace wivrn
