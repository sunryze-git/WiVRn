// SPDX-License-Identifier: GPL-3.0-or-later
#include "clock_offset.h"
#include "pose_list.h"
#include <Eigen/Geometry>
#include <cmath>
#include <iostream>
#include <source_location>
#include <string_view>

using namespace wyrm;

static int failures = 0;
static void check(bool valid, std::source_location where = std::source_location::current())
{
	if (not valid)
	{
		std::cerr << where.file_name() << ':' << where.line() << '\n';
		++failures;
	}
}

static constexpr XrTime epoch = 10'000'000'000;

static Eigen::Vector3f vector(const xrt_vec3 & v)
{
	return {v.x, v.y, v.z};
}

static Eigen::Quaternionf quaternion(const xrt_quat & q)
{
	return {q.w, q.x, q.y, q.z};
}

static void add_rotation(pose_list & poses, XrDuration time, Eigen::Vector3f omega, bool negative = false, bool velocity = true)
{
	const Eigen::Quaternionf initial(Eigen::AngleAxisf(0.7f, Eigen::Vector3f(1, 2, 3).normalized()));
	Eigen::Quaternionf q = Eigen::Quaternionf(Eigen::AngleAxisf(omega.norm() * time * 1e-9f, omega.normalized())) * initial;
	if (negative)
		q.coeffs() *= -1;

	from_headset::tracking packet{};
	packet.production_timestamp = packet.timestamp = epoch + time;
	packet.device_poses.push_back({
	        .pose = {.orientation = {.x = q.x(), .y = q.y(), .z = q.z(), .w = q.w()}},
	        .linear_velocity = {},
	        .angular_velocity = {.x = omega.x(), .y = omega.y(), .z = omega.z()},
	        .device = device_id::LEFT_GRIP,
	        .flags = uint8_t(from_headset::pose_flags::orientation_valid | from_headset::pose_flags::position_valid |
	                         from_headset::pose_flags::linear_velocity_valid |
	                         (velocity ? from_headset::pose_flags::angular_velocity_valid : 0)),
	});
	poses.update_tracking(packet, {.stable = true});
}

static void test_rotation()
{
	// Each world-space axis and a mixed axis, at a nonidentity orientation.
	for (Eigen::Vector3f omega: {Eigen::Vector3f(12, 0, 0), Eigen::Vector3f(0, 12, 0), Eigen::Vector3f(0, 0, 12), Eigen::Vector3f(3, -8, 6)})
	{
		pose_list poses(device_id::LEFT_GRIP);
		add_rotation(poses, 0, omega);
		auto [produced, result] = poses.get_at(epoch);
		check((result.relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) != 0);
		check((vector(result.angular_velocity) - omega).norm() < 0.001f);
	}

	// q and -q must yield the same fitted pose AND velocity, including replacement.
	pose_list positive(device_id::LEFT_GRIP), alternating(device_id::LEFT_GRIP);
	const Eigen::Vector3f omega(3, -8, 6);
	for (int i = 0; i < 12; ++i)
	{
		add_rotation(positive, i * 8'000'000, omega);
		add_rotation(alternating, i * 8'000'000, omega, i % 2);
	}
	for (XrDuration t: {80'000'000, 88'000'000, 100'000'000})
	{
		auto a = positive.get_at(epoch + t).second;
		auto b = alternating.get_at(epoch + t).second;
		check(quaternion(a.pose.orientation).angularDistance(quaternion(b.pose.orientation)) < 0.0001f);
		check((vector(a.angular_velocity) - vector(b.angular_velocity)).norm() < 0.001f);
	}
	add_rotation(positive, 88'500'000, omega);
	add_rotation(alternating, 88'500'000, omega, true);
	check((vector(positive.get_at(epoch + 100'000'000).second.angular_velocity) - vector(alternating.get_at(epoch + 100'000'000).second.angular_velocity)).norm() < 0.001f);
}

static void test_normalization()
{
	// Three samples determine one quadratic independently of the fit's weights.
	// Its normalized derivative must describe the returned orientation.
	pose_list poses(device_id::LEFT_GRIP);
	const Eigen::Vector3f omega(0, 25, 0);
	for (int i = 0; i < 3; ++i)
		add_rotation(poses, i * 16'000'000, omega, false, false);
	const XrTime at = epoch + 64'000'000;
	auto result = poses.get_at(at).second;
	// Differentiate the normalized Lagrange polynomial analytically. The fixed
	// initial rotation cancels, leaving the sine/cosine of the half angle.
	const double h = 0.016, t = 0.064;
	Eigen::Vector2d q = Eigen::Vector2d::Zero(), dq = Eigen::Vector2d::Zero();
	const double weights[]{(t - h) * (t - 2 * h) / (2 * h * h), -t * (t - 2 * h) / (h * h), t * (t - h) / (2 * h * h)};
	const double derivatives[]{(2 * t - 3 * h) / (2 * h * h), -(2 * t - 2 * h) / (h * h), (2 * t - h) / (2 * h * h)};
	for (int i = 0; i < 3; ++i)
	{
		Eigen::Vector2d value(std::cos(12.5 * i * h), std::sin(12.5 * i * h));
		q += weights[i] * value;
		dq += derivatives[i] * value;
	}
	const float expected = 2 * (q.x() * dq.y() - q.y() * dq.x()) / q.squaredNorm();
	check((vector(result.angular_velocity) - Eigen::Vector3f(0, expected, 0)).norm() < 0.03f);
}

static void test_derived_velocity()
{
	pose_list grip(device_id::LEFT_GRIP), aim(device_id::LEFT_AIM);
	const Eigen::Vector3f omega(0, 10, 0);
	for (int i = 0; i < 8; ++i)
		add_rotation(grip, i * 8'000'000, omega);
	xrt_pose offset = XRT_POSE_IDENTITY;
	offset.position = {0, 0, -0.2f};
	aim.set_derived(&grip, offset);
	auto result = std::get<1>(aim.get_pose_at(epoch + 56'000'000));
	auto source = grip.get_at(epoch + 56'000'000).second;
	Eigen::Vector3f expected = vector(source.linear_velocity) + vector(source.angular_velocity).cross(quaternion(source.pose.orientation) * vector(offset.position));
	check((vector(result.linear_velocity) - expected).norm() < 0.001f);
	check(result.relation_flags == source.relation_flags);
	check(quaternion(result.pose.orientation).angularDistance(quaternion(source.pose.orientation)) < 0.0001f);
	check((vector(result.pose.position) - quaternion(source.pose.orientation) * vector(offset.position)).norm() < 0.0001f);

	// Losing positional tracking must also invalidate derived positions.
	grip.reset();
	from_headset::tracking packet{};
	packet.production_timestamp = packet.timestamp = epoch;
	packet.device_poses.push_back({
	        .pose = {.orientation = {.w = 1}},
	        .device = device_id::LEFT_GRIP,
	        .flags = from_headset::pose_flags::orientation_valid,
	});
	grip.update_tracking(packet, {.stable = true});
	result = std::get<1>(aim.get_pose_at(epoch));
	check((result.relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) == 0);
}

static void test_translation()
{
	polynomial_interpolator<3> positions;
	check(not positions.get_at(epoch).y);
	for (int i = 0; i < 20; ++i)
	{
		float t = i * 0.008f;
		positions.add_sample({epoch + i * 8'000'000, epoch + i * 8'000'000, Eigen::Vector3f(1 + 2 * t + 3 * t * t, -t, 4), Eigen::Vector3f(2 + 6 * t, -1, 0)});
	}
	for (XrDuration ns: {100'000'000, 152'000'000, 170'000'000})
	{
		float t = ns * 1e-9f;
		auto result = positions.get_at(epoch + ns);
		check(result.y and (*result.y - Eigen::Vector3f(1 + 2 * t + 3 * t * t, -t, 4)).norm() < 0.0001f);
		check(result.dy and (*result.dy - Eigen::Vector3f(2 + 6 * t, -1, 0)).norm() < 0.001f);
	}
	// Requests too far in the past must be rejected, even beyond 32-bit ns.
	check(not positions.get_at(epoch - 5'000'000'000).y);
	positions.reset();
	check(not positions.get_at(epoch).y);
}

static void test_sample_management()
{
	pose_list empty(device_id::LEFT_GRIP);
	auto [produced, relation] = empty.get_at(epoch);
	check(produced == 0 and relation.relation_flags == 0);

	using interpolator = polynomial_interpolator<3>;
	const Eigen::Vector3f position(1, 2, 3), velocity(2, -1, 0);
	interpolator samples;
	samples.add_sample({epoch, epoch, position, velocity});
	auto result = samples.get_at(epoch + 10'000'000);
	check(result.y and (*result.y - position - 0.01f * velocity).norm() < 0.0001f);

	// A delayed packet must not replace a newer prediction of the same time.
	samples.add_sample({epoch - 1'000'000, epoch, Eigen::Vector3f::Zero(), velocity});
	check((*samples.get_at(epoch).y - position).norm() < 0.0001f);
	samples.add_sample({epoch + 1'000'000, epoch, position * 2, velocity});
	check((*samples.get_at(epoch).y - position * 2).norm() < 0.0001f);

	// Bounded extrapolation during a network stall.
	const XrTime limit = epoch + 1'000'000 + max_extrapolation_ns * 11 / 10;
	auto limited = samples.get_at(limit);
	auto stalled = samples.get_at(limit + 1'000'000'000);
	check(stalled.timestamp == limit);
	check(stalled.y and (*stalled.y - *limited.y).norm() < 0.0001f);

	// Runtime-predicted samples have distinct production and target times.
	samples.reset();
	samples.add_sample({epoch, epoch + 50'000'000, position, velocity});
	check((*samples.get_at(epoch + 50'000'000).y - position).norm() < 0.0001f);
	check(samples.get_at(epoch + 50'000'000).production_timestamp == epoch);

	// Invalid tracking and subsequent recovery must not reuse invalid values.
	samples.add_sample({epoch + 60'000'000, epoch + 60'000'000});
	check(not samples.get_at(epoch + 60'000'000).y);
	samples.add_sample({epoch + 70'000'000, epoch + 70'000'000, position, velocity});
	check(samples.get_at(epoch + 70'000'000).y.has_value());
}

static void test_tracking_confidence()
{
	using flags = from_headset::pose_flags;
	for (uint8_t tracked: {uint8_t(0), uint8_t(flags::position_tracked), uint8_t(flags::orientation_tracked), uint8_t(flags::position_tracked | flags::orientation_tracked)})
	{
		pose_list grip(device_id::LEFT_GRIP), aim(device_id::LEFT_AIM);
		for (int i = 0; i < 8; ++i)
		{
			from_headset::tracking packet{};
			packet.production_timestamp = packet.timestamp = epoch + i * 6'000'000;
			packet.device_poses.push_back({
			        .pose = {.orientation = {.w = 1}},
			        .device = device_id::LEFT_GRIP,
			        .flags = uint8_t(flags::position_valid | flags::orientation_valid | (i < 4 ? flags::position_tracked | flags::orientation_tracked : tracked)),
			});
			grip.update_tracking(packet, {.stable = true});
		}
		auto result = grip.get_at(epoch + 39'000'000).second;
		check((result.relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) != 0);
		check((result.relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0);
		check(bool(result.relation_flags & XRT_SPACE_RELATION_POSITION_TRACKED_BIT) == bool(tracked & flags::position_tracked));
		check(bool(result.relation_flags & XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT) == bool(tracked & flags::orientation_tracked));
		xrt_pose offset = XRT_POSE_IDENTITY;
		offset.position.z = -0.2f;
		aim.set_derived(&grip, offset);
		check(std::get<1>(aim.get_pose_at(epoch + 39'000'000)).relation_flags == result.relation_flags);
	}
}

// A grip spinning about +Y with angle(t) and rate(t), sampled unpredicted like the headset sends controllers
static void add_spin(pose_list & poses, XrDuration time, float angle, float rate)
{
	const Eigen::Quaternionf q(Eigen::AngleAxisf(angle, Eigen::Vector3f::UnitY()));
	from_headset::tracking packet{};
	packet.production_timestamp = packet.timestamp = epoch + time;
	packet.device_poses.push_back({
	        .pose = {.orientation = {.x = q.x(), .y = q.y(), .z = q.z(), .w = q.w()}},
	        .linear_velocity = {},
	        .angular_velocity = {.x = 0, .y = rate, .z = 0},
	        .device = device_id::LEFT_GRIP,
	        .flags = uint8_t(from_headset::pose_flags::orientation_valid | from_headset::pose_flags::position_valid |
	                         from_headset::pose_flags::linear_velocity_valid | from_headset::pose_flags::angular_velocity_valid),
	});
	poses.update_tracking(packet, {.stable = true});
}

// Largest error, in radians, of poses predicted 55 ms ahead while a grip follows angle(t) from `from` to `to` seconds
static float predicted_angle_error(auto angle, float from, float to)
{
	pose_list poses(device_id::LEFT_GRIP);
	poses.predict_motion();
	constexpr XrDuration step = 6'944'444, ahead = 55'000'000;
	float worst = 0;
	for (XrDuration time = 0; time * 1e-9f < to; time += step)
	{
		const float t = time * 1e-9f;
		add_spin(poses, time, angle(t), (angle(t + 0.0005f) - angle(t - 0.0005f)) / 0.001f);
		if (t < from)
			continue;
		const auto result = poses.get_at(epoch + time + ahead).second;
		const Eigen::Quaternionf expected(Eigen::AngleAxisf(angle(t + ahead * 1e-9f), Eigen::Vector3f::UnitY()));
		worst = std::max(worst, quaternion(result.pose.orientation).angularDistance(expected));
	}
	return worst;
}

static void test_controller_prediction()
{
	constexpr float degree = M_PI / 180;
	// A steady spin continues exactly
	check(predicted_angle_error([](float t) { return 15 * t; }, 0.5f, 1) < 0.5f * degree);
	// A wrist flicking 45° each way 5 times a second turns back within the 55 ms: last pose plus velocity is up to
	// 60° off there
	const auto flick = [](float t) { return 0.785f * std::sin(31.4f * t); };
	check(predicted_angle_error(flick, 1, 2) < 5 * degree);
	// Once the flicking has stopped for 150 ms, so has the prediction
	check(predicted_angle_error([&](float t) { return t < 1 ? flick(t) : flick(1); }, 1.15f, 1.5f) < 1 * degree);
	// A controller at rest stays put
	check(predicted_angle_error([](float) { return 0.3f; }, 0.5f, 1) < 0.01f * degree);
}

int main(int argc, char ** argv)
{
	std::string_view selected = argc > 1 ? argv[1] : "all";
	if (selected == "all" or selected == "rotation")
		test_rotation();
	if (selected == "all" or selected == "normalization")
		test_normalization();
	if (selected == "all" or selected == "derived")
		test_derived_velocity();
	if (selected == "all" or selected == "translation")
		test_translation();
	if (selected == "all" or selected == "samples")
		test_sample_management();
	if (selected == "all" or selected == "confidence")
		test_tracking_confidence();
	if (selected == "all" or selected == "controllers")
		test_controller_prediction();
	std::cout << "Pose prediction (" << selected << "): " << failures << " failures\n";
	return failures ? 1 : 0;
}
