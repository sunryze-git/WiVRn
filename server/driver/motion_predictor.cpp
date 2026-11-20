/*
 * WiVRn VR streaming
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "motion_predictor.h"

#include <algorithm>
#include <cmath>

namespace wivrn
{
namespace
{
// Samples are put on a fixed step whatever the headset's rate, so every span below is a count of these
constexpr double step_s = 1.0 / 144;
constexpr XrDuration step_ns = 6'944'444;
constexpr XrDuration max_gap_ns = 100'000'000;

constexpr int query = 18;       // samples of motion looked up in the memory
constexpr int newest_span = 4;  // of which the newest count twice
constexpr int scale_span = 6;   // samples that set how much bigger or smaller the motion is now
constexpr int lag_min = 14;     // more than the steps a prediction may look ahead
constexpr int lag_max = 1152;   // 8 s
constexpr int matches = 64;     // remembered motions averaged: fewer follow the motion closer but make the pose jitter
constexpr double noise = 0.002; // rad per sample
constexpr double scale_min = 0.6, scale_max = 1.5;
constexpr int rest_span = 24;
constexpr double rest_speed = 0.5, moving_speed = 2; // rad/s, or 10 * m/s

Eigen::Vector3d rotation_vector(Eigen::Quaterniond q)
{
	if (q.w() < 0)
		q.coeffs() *= -1;
	const double n = q.vec().norm();
	if (n < 1e-9)
		return 2 * q.vec();
	return q.vec() / n * 2 * std::atan2(n, q.w());
}

Eigen::Quaterniond from_rotation_vector(const Eigen::Vector3d & r)
{
	const double angle = r.norm();
	if (angle < 1e-9)
		return Eigen::Quaterniond(1, r.x() / 2, r.y() / 2, r.z() / 2).normalized();
	return Eigen::Quaterniond(Eigen::AngleAxisd(angle, r / angle));
}

double ramp(double value, double low, double high)
{
	return std::clamp((value - low) / (high - low), 0., 1.);
}
} // namespace

void motion_predictor::add_sample(XrTime timestamp, const pose & value)
{
	if (not samples.empty())
	{
		if (timestamp <= samples.back().timestamp)
			return;
		if (timestamp - samples.back().timestamp > max_gap_ns)
			samples.clear();
	}
	samples.push_back({timestamp, value});
	if (samples.size() > 1 and samples.back().value.orientation.dot(samples[samples.size() - 2].value.orientation) < 0)
		samples.back().value.orientation.coeffs() *= -1;
	while (samples.back().timestamp - samples.front().timestamp > (lag_max + query + 4) * step_ns)
		samples.pop_front();
	stale = true;
}

void motion_predictor::reset()
{
	samples.clear();
	stale = true;
}

std::optional<XrTime> motion_predictor::newest() const
{
	if (samples.empty())
		return {};
	return samples.back().timestamp;
}

void motion_predictor::rebuild()
{
	stale = false;
	moving = 0;

	// The samples on a fixed step back from the newest
	const int wanted = lag_max + query + 2;
	std::vector<pose> grid;
	grid.reserve(wanted);
	const XrTime newest = samples.back().timestamp;
	size_t i = samples.size() - 1;
	for (int j = 0; j < wanted; ++j)
	{
		const XrTime at = newest - j * step_ns;
		while (i > 0 and samples[i - 1].timestamp >= at)
			--i;
		if (i == 0)
		{
			if (samples[0].timestamp == at)
				grid.push_back(samples[0].value);
			break;
		}
		const auto & a = samples[i - 1];
		const auto & b = samples[i];
		const double f = double(at - a.timestamp) / double(b.timestamp - a.timestamp);
		grid.push_back({
		        .position = a.value.position + f * (b.value.position - a.value.position),
		        .orientation = from_rotation_vector(f * rotation_vector(b.value.orientation * a.value.orientation.conjugate())) * a.value.orientation,
		        .linear_velocity = a.value.linear_velocity + f * (b.value.linear_velocity - a.value.linear_velocity),
		        .angular_velocity = a.value.angular_velocity + f * (b.value.angular_velocity - a.value.angular_velocity),
		});
	}

	// Per sample, in the controller's own frame: rotation and translation since the previous one
	const int count = int(grid.size()) - 1;
	if (count < lag_min + query + 1)
		return;
	std::vector<Eigen::Vector3d> turn(count), shift(count);
	for (int j = 0; j < count; ++j)
	{
		const Eigen::Quaterniond before = grid[j + 1].orientation.conjugate();
		turn[j] = rotation_vector(before * grid[j].orientation);
		shift[j] = before * (grid[j].position - grid[j + 1].position);
	}

	// Where did the last `query` samples of motion happen before? The newest ones count twice, so a match is dropped
	// quickly when the controller stops following it.
	const auto weighted = [&](auto && term) {
		double sum = 0;
		for (int k = 0; k < query; ++k)
			sum += (k < newest_span ? 2 : 1) * term(k);
		return sum;
	};
	struct match
	{
		double distance;
		int lag;
	};
	std::vector<match> found;
	found.reserve(count);
	for (int lag = lag_min; lag + query < count; ++lag)
		found.push_back({weighted([&](int k) { return (turn[k] - turn[k + lag]).squaredNorm(); }), lag});
	if (found.size() > matches)
	{
		std::ranges::nth_element(found, found.begin() + matches, {}, &match::distance);
		found.resize(matches);
	}
	const double floor = (query + newest_span) * noise * noise;
	const double best = std::ranges::min_element(found, {}, &match::distance)->distance;

	double speed = 0;
	for (int j = 0; j < rest_span; ++j)
		speed = std::max({speed, grid[j].angular_velocity.norm(), 10 * grid[j].linear_velocity.norm()});
	moving = ramp(speed, rest_speed, moving_speed);
	if (moving == 0)
		return;

	// For each match: what followed, less what its reported velocity promised, sized to the motion now. Averaged by
	// how close the match is.
	correction.fill({});
	double size_now = 0;
	for (int k = 0; k < scale_span; ++k)
		size_now += turn[k].squaredNorm();
	double total = 0;
	for (const auto & m: found)
	{
		double size_then = 0;
		for (int k = 0; k < scale_span; ++k)
			size_then += turn[k + m.lag].squaredNorm();
		const double scale = std::clamp(std::sqrt(size_now / std::max(size_then, 1e-12)), scale_min, scale_max);
		const double closeness = (best + floor) / (m.distance + floor);
		const double weight = closeness * closeness;
		const pose & then = grid[m.lag];
		const Eigen::Vector3d promised_turn = then.orientation.conjugate() * then.angular_velocity * (scale * step_s);
		const Eigen::Vector3d promised_shift = then.orientation.conjugate() * then.linear_velocity * (scale * step_s);
		Eigen::Quaterniond q = Eigen::Quaterniond::Identity();
		Eigen::Vector3d p = Eigen::Vector3d::Zero();
		for (int k = 1; k <= reach; ++k)
		{
			p += q * (shift[m.lag - k] * scale);
			q = q * from_rotation_vector(turn[m.lag - k] * scale);
			correction[k].rotation += weight * (rotation_vector(q) - k * promised_turn);
			correction[k].position += weight * (p - k * promised_shift);
		}
		total += weight;
	}
	const Eigen::Quaterniond & frame = grid[0].orientation;
	for (auto & c: correction)
	{
		c.rotation = frame * (c.rotation * (moving / total));
		c.position = frame * (c.position * (moving / total));
	}
}

motion_predictor::steps motion_predictor::step(double ahead) const
{
	const pose & newest = samples.back().value;
	steps result{ahead * newest.angular_velocity, ahead * newest.linear_velocity};
	if (moving > 0)
	{
		const double count = ahead / step_s;
		const int whole = std::min(int(count), reach - 1);
		const double part = count - whole;
		const steps & a = correction[whole];
		const steps & b = correction[whole + 1];
		result.rotation += a.rotation + part * (b.rotation - a.rotation);
		result.position += a.position + part * (b.position - a.position);
	}
	return result;
}

std::optional<motion_predictor::pose> motion_predictor::predict(XrTime at)
{
	if (samples.empty())
		return {};
	if (stale)
		rebuild();

	const auto & newest = samples.back();
	const double ahead = std::clamp((at - newest.timestamp) * 1e-9, 0., reach * step_s);
	constexpr double h = 0.002;
	const steps now = step(ahead), before = step(std::max(ahead - h, 0.)), after = step(std::min(ahead + h, reach * step_s));
	const double span = std::min(ahead + h, reach * step_s) - std::max(ahead - h, 0.);
	return pose{
	        .position = newest.value.position + now.position,
	        .orientation = from_rotation_vector(now.rotation) * newest.value.orientation,
	        .linear_velocity = (after.position - before.position) / span,
	        .angular_velocity = (after.rotation - before.rotation) / span,
	};
}

} // namespace wivrn
