/*
 * Wyrm VR streaming
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "wyrm_packets.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace frame_eyes
{
// Lid openness, widen and pupil dilation from the Steam Frame's eye cameras (Babble's two-stage eye kit on
// dsp_service frames), with per-eye gaze from Valve's eye tracker
class tracker
{
public:
	using sender = std::function<void(wyrm::from_headset::frame_eyes &&)>;

	// model_dir holds the eye kit (eye_kit_stage1_fp16.pte, eye_kit_stage2_fp16.pte); one update per `divisor` camera frames
	tracker(std::filesystem::path model_dir, uint32_t divisor, sender send, std::function<XrTime()> now);
	tracker(const tracker &) = delete;
	tracker & operator=(const tracker &) = delete;
	~tracker();

	// Pauses the cameras' reader and the model while the headset is off; putting it on starts a new calibration
	void set_present(bool present);
	// Guided calibration: "neutral" (relaxed, eyes open) or "closed" (eyes fully closed) for the next few seconds;
	// seeds the automatic calibration, which keeps adjusting afterwards
	void calibrate(std::string_view step);
	std::string statistics_json();

	struct statistics
	{
		std::string state = "starting";
		float update_hz = 0;
		bool calibrated = false;
		bool gaze = false;
		// The last guided step's result, numbered so a caller can tell a new result from an old one
		uint32_t calibration_count = 0;
		std::string calibration;
	};

private:
	struct impl;
	std::unique_ptr<impl> d;
	std::thread thread;
	std::atomic<bool> quit{false};
	std::atomic<bool> present{true};
	std::atomic<bool> new_session{false};
	std::mutex calibration_mutex;
	std::string calibration_request;
	std::mutex stats_mutex;
	statistics stats;

	void run();
};
} // namespace frame_eyes
