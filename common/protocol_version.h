#include "wyrm_packets.h"
#include "wyrm_serialization.h"

#pragma once

namespace wyrm
{
using protocol = std::variant<from_headset::packets, to_headset::packets>;
static inline const constinit auto protocol_version = serialization_type_hash<protocol>(protocol_revision);
} // namespace wyrm
