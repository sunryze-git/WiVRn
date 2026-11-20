// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace wivrn
{
constexpr bool image_queue_transfer_is_implicit(uint32_t from, uint32_t to, bool maintenance9 = false, uint32_t transfer_mask = 0)
{
	return from == UINT32_MAX or to == UINT32_MAX or from == to or
	       (maintenance9 and to < 32 and (transfer_mask & (1u << to)) != 0);
}
} // namespace wivrn
