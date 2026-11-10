// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nvenc_options.h"
#include <algorithm>
#include <ffnvcodec/nvEncodeAPI.h>
namespace wivrn
{
inline NV_ENC_RC_PARAMS nvenc_rate_control(const nvenc_options & options, uint64_t bitrate, float framerate)
{
	NV_ENC_RC_PARAMS rc{};
	rc.rateControlMode = options.rc == nvenc_options::rate_control::cqp ? NV_ENC_PARAMS_RC_CONSTQP : options.rc == nvenc_options::rate_control::vbr ? NV_ENC_PARAMS_RC_VBR
	                                                                                                                                                : NV_ENC_PARAMS_RC_CBR;
	rc.averageBitRate = bitrate;
	rc.maxBitRate = std::max<uint64_t>(options.max_bitrate, bitrate);
	rc.vbvBufferSize = std::min<double>(UINT32_MAX, bitrate / double(framerate) * options.vbv_frames);
	rc.vbvInitialDelay = std::min<double>(UINT32_MAX, bitrate / double(framerate) * options.vbv_initial_frames);
	rc.enableAQ = options.aq;
	rc.aqStrength = options.aq_strength;
	rc.enableTemporalAQ = options.temporal_aq;
	rc.enableNonRefP = 1;
	rc.lowDelayKeyFrameScale = 1;
	rc.multiPass = options.multipass == nvenc_options::passes::full ? NV_ENC_TWO_PASS_FULL_RESOLUTION : options.multipass == nvenc_options::passes::quarter ? NV_ENC_TWO_PASS_QUARTER_RESOLUTION
	                                                                                                                                                        : NV_ENC_MULTI_PASS_DISABLED;
	rc.constQP = {options.qp, options.qp, options.qp};
	if (options.min_qp)
	{
		rc.enableMinQP = 1;
		rc.minQP = {*options.min_qp, *options.min_qp, *options.min_qp};
	}
	if (options.max_qp)
	{
		rc.enableMaxQP = 1;
		rc.maxQP = {*options.max_qp, *options.max_qp, *options.max_qp};
	}
	return rc;
}
} // namespace wivrn
