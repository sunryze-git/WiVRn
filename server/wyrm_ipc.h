/*
 * Wyrm VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "wyrm_packets.h"
#include "wyrm_sockets.h"

#include <memory>
#include <optional>
#include <stdint.h>
#include <variant>

namespace wyrm
{
class wyrm_connection;
}

extern std::unique_ptr<wyrm::wyrm_connection> connection;

namespace from_monado
{
struct headset_connected
{};

struct headset_disconnected
{};

struct server_error
{
	std::string where;
	std::string message;
};

using packets = std::variant<
        wyrm::from_headset::headset_info_packet,
        wyrm::from_headset::settings_changed,
        wyrm::from_headset::start_app,
        wyrm::from_headset::stream_tab_changed,
        headset_connected,
        headset_disconnected,
        server_error>;
} // namespace from_monado

namespace to_monado
{
struct stop
{};

struct disconnect
{};

struct set_bitrate
{
	uint32_t bitrate_bps;
};

using packets = std::variant<stop, disconnect, set_bitrate, wyrm::to_headset::stream_tab_change>;
} // namespace to_monado

extern std::optional<wyrm::typed_socket<wyrm::UnixDatagram, to_monado::packets, from_monado::packets>> wyrm_ipc_socket_monado;

std::optional<to_monado::packets> receive_from_main();
template <typename T>
void send_to_main(T packet)
{
	wyrm_ipc_socket_monado->send(std::move(packet));
}
