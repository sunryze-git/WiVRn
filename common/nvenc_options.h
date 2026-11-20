// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace wivrn
{
struct nvenc_options
{
	unsigned preset = 4;
	enum class tuning
	{
		ultra_low_latency,
		low_latency,
		high_quality
	} tuning = tuning::ultra_low_latency;
	enum class passes
	{
		disabled,
		quarter,
		full
	} multipass = passes::quarter;
	enum class rate_control
	{
		cbr,
		vbr,
		cqp
	} rc = rate_control::cbr;
	bool aq = true, temporal_aq = false;
	unsigned aq_strength = 0, qp = 23;
	std::optional<unsigned> min_qp, max_qp;
	uint32_t max_bitrate = 0, gop = UINT32_MAX;
	unsigned intra_refresh_period = 100, intra_refresh_count = 50;
	float vbv_frames = 2, vbv_initial_frames = 1;

	explicit nvenc_options(const std::map<std::string, std::string> & values = {})
	{
		for (const auto & [key, value]: values)
		{
			auto invalid = [&]() -> std::runtime_error { return std::runtime_error("nvenc: invalid " + key + " = " + value); };
			auto number = [&](float low, float high) {
				float result;
				auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
				if (error != std::errc{} or end != value.data() + value.size() or not std::isfinite(result) or result < low or result > high)
					throw invalid();
				return result;
			};
			auto integer = [&](uint32_t low, uint32_t high) {
				uint32_t result;
				auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
				if (error != std::errc{} or end != value.data() + value.size() or result < low or result > high)
					throw invalid();
				return result;
			};
			auto boolean = [&]() {
				if (value == "true" or value == "1")
					return true;
				if (value == "false" or value == "0")
					return false;
				throw invalid();
			};
			if (key == "preset")
			{
				if (value.size() != 2 or (value[0] != 'p' and value[0] != 'P') or value[1] < '1' or value[1] > '7')
					throw invalid();
				preset = value[1] - '0';
			}
			else if (key == "tuning")
			{
				if (value == "ultra-low-latency")
					tuning = tuning::ultra_low_latency;
				else if (value == "low-latency")
					tuning = tuning::low_latency;
				else if (value == "high-quality")
					tuning = tuning::high_quality;
				else
					throw invalid();
			}
			else if (key == "multipass")
			{
				if (value == "disabled")
					multipass = passes::disabled;
				else if (value == "quarter")
					multipass = passes::quarter;
				else if (value == "full")
					multipass = passes::full;
				else
					throw invalid();
			}
			else if (key == "rc")
			{
				if (value == "cbr")
					rc = rate_control::cbr;
				else if (value == "vbr")
					rc = rate_control::vbr;
				else if (value == "cqp")
					rc = rate_control::cqp;
				else
					throw invalid();
			}
			else if (key == "aq")
				aq = boolean();
			else if (key == "temporal-aq")
				temporal_aq = boolean();
			else if (key == "aq-strength")
				aq_strength = integer(0, 15);
			else if (key == "vbv-frames")
				vbv_frames = number(1, 16);
			else if (key == "vbv-initial-frames")
				vbv_initial_frames = number(0, 16);
			else if (key == "qp")
				qp = integer(0, 51);
			else if (key == "min-qp")
				min_qp = integer(0, 51);
			else if (key == "max-qp")
				max_qp = integer(0, 51);
			else if (key == "max-bitrate")
				max_bitrate = integer(1, UINT32_MAX);
			else if (key == "gop")
				gop = integer(1, UINT32_MAX);
			else if (key == "intra-refresh-period")
				intra_refresh_period = integer(0, 10000);
			else if (key == "intra-refresh-count")
				intra_refresh_count = integer(1, 10000);
			else if (key == "lookahead" or key == "b-frames")
			{
				if (value != "0")
					throw std::runtime_error("nvenc: lookahead and B-frames require buffering and are unsupported by this streaming pipeline");
			}
			else
				throw std::runtime_error("nvenc: unknown option " + key);
		}
		if (vbv_initial_frames > vbv_frames)
			throw std::runtime_error("nvenc: initial VBV exceeds buffer size");
		if (min_qp and max_qp and *min_qp > *max_qp)
			throw std::runtime_error("nvenc: min-qp exceeds max-qp");
		if (intra_refresh_period and intra_refresh_count > intra_refresh_period)
			throw std::runtime_error("nvenc: intra-refresh-count exceeds period");
	}
};
} // namespace wivrn
