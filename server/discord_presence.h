// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace wyrm
{
std::vector<std::filesystem::path> discord_ipc_paths();

// Driven by the server's main loop; every socket operation is nonblocking.
class discord_presence
{
public:
	using clock = std::chrono::steady_clock;

	explicit discord_presence(std::vector<std::filesystem::path> paths = discord_ipc_paths());
	~discord_presence();
	discord_presence(const discord_presence &) = delete;
	discord_presence & operator=(const discord_presence &) = delete;

	void configure(bool enabled, std::string application_id);
	void set_connected(bool connected);
	void set_headset(std::string name);
	void set_application(std::string name);
	void poll(clock::time_point now = clock::now());
	void after_fork_child();

private:
	enum class state
	{
		disconnected,
		connecting,
		handshake,
		ready
	};
	const pid_t owner_pid;
	const std::vector<std::filesystem::path> paths;
	bool enabled = false;
	bool headset_connected = false;
	bool dirty = true;
	std::string application_id;
	std::string headset;
	std::string application;
	int64_t started = 0;
	int fd = -1;
	state connection_state = state::disconnected;
	clock::time_point retry_at{}, deadline{}, next_update{};
	std::chrono::seconds retry_delay{1};
	uint64_t nonce = 0;
	std::vector<uint8_t> incoming, outgoing;
	size_t written = 0;

	void disconnect(bool clear_activity = false);
	void retry(clock::time_point now);
	void connect(clock::time_point now);
	bool queue(uint32_t opcode, std::string_view payload);
	bool flush();
	bool receive(clock::time_point now);
	nlohmann::json activity() const;
	nlohmann::json command(nlohmann::json activity);
};
} // namespace wyrm
