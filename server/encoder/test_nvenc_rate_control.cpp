// SPDX-License-Identifier: GPL-3.0-or-later
#include "nvenc_rate_control.h"
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
	using namespace wyrm;
	nvenc_options quality({{"preset", "P6"}, {"multipass", "full"}, {"aq-strength", "9"}, {"temporal-aq", "true"}, {"vbv-frames", "3"}, {"rc", "vbr"}, {"max-bitrate", "200000000"}, {"min-qp", "10"}, {"max-qp", "45"}});
	for (uint32_t bitrate: {10'000'000u, 80'000'000u, 250'000'000u})
		for (float rate: {45.0f, 72.0f, 90.0f, 119.88f})
		{
			const auto rc = nvenc_rate_control(quality, bitrate, rate);
			check(rc.rateControlMode == NV_ENC_PARAMS_RC_VBR);
			check(rc.averageBitRate == bitrate and rc.maxBitRate >= bitrate);
			check(rc.multiPass == NV_ENC_TWO_PASS_FULL_RESOLUTION);
			check(rc.enableAQ and rc.aqStrength == 9 and rc.enableTemporalAQ and rc.enableNonRefP);
			check(not rc.enableLookahead and rc.lookaheadDepth == 0);
			check(rc.enableMinQP and rc.minQP.qpInterP == 10 and rc.enableMaxQP and rc.maxQP.qpInterP == 45);
			check(std::abs(double(rc.vbvBufferSize) - bitrate / double(rate) * 3) < 1);
		}
	nvenc_options cqp({{"rc", "cqp"}, {"qp", "18"}, {"multipass", "disabled"}, {"aq", "false"}});
	const auto rc = nvenc_rate_control(cqp, 80'000'000, 90);
	check(rc.rateControlMode == NV_ENC_PARAMS_RC_CONSTQP and rc.constQP.qpInterP == 18 and rc.constQP.qpIntra == 18);
	check(not rc.enableAQ and rc.multiPass == NV_ENC_MULTI_PASS_DISABLED);
	const auto defaults = nvenc_rate_control(nvenc_options{}, 80'000'000, 90);
	check(defaults.rateControlMode == NV_ENC_PARAMS_RC_CBR and defaults.enableAQ and defaults.enableNonRefP);
	nvenc_options large({{"vbv-frames", "16"}});
	check(nvenc_rate_control(large, UINT32_MAX, 10).vbvBufferSize == UINT32_MAX);
	std::cout << "NVENC quality flags persist across bitrate and fractional refresh changes\n";
}
