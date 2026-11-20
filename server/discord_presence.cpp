// SPDX-License-Identifier: GPL-3.0-or-later
#include "discord_presence.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{
constexpr size_t max_payload = 64 * 1024;

uint32_t read_u32(const uint8_t * bytes)
{
	return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}

void append_u32(std::vector<uint8_t> & bytes, uint32_t value)
{
	for (unsigned shift = 0; shift < 32; shift += 8)
		bytes.push_back(uint8_t(value >> shift));
}

std::string field(std::string_view value)
{
	size_t length = std::min<size_t>(value.size(), 128);
	while (length < value.size() and length > 0 and (uint8_t(value[length]) & 0xc0) == 0x80)
		--length;
	std::string result(value.substr(0, length));
	for (char & c: result)
		if (static_cast<unsigned char>(c) < 0x20)
			c = ' ';
	return result;
}

std::string encode(const nlohmann::json & value)
{
	return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}
} // namespace

namespace wivrn
{
std::vector<std::filesystem::path> discord_ipc_paths()
{
	if (const char * path = std::getenv("WIVRN_DISCORD_IPC_PATH"); path and *path)
		return {path};

	std::vector<std::filesystem::path> directories;
	auto add = [&](const std::filesystem::path & directory) {
		if (not directory.empty() and std::ranges::find(directories, directory) == directories.end())
			directories.push_back(directory);
	};
	for (const char * key: {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"})
		if (const char * value = std::getenv(key); value and *value)
			add(value);
	add(std::filesystem::path("/run/user") / std::to_string(getuid()));
	add("/tmp");

	std::vector<std::filesystem::path> paths;
	for (const auto & directory: directories)
	{
		for (int index = 0; index < 10; ++index)
			paths.push_back(directory / ("discord-ipc-" + std::to_string(index)));
		for (const char * app: {"com.discordapp.Discord", "dev.vencord.Vesktop"})
			for (int index = 0; index < 10; ++index)
				paths.push_back(directory / "app" / app / ("discord-ipc-" + std::to_string(index)));
	}
	return paths;
}

discord_presence::discord_presence(std::vector<std::filesystem::path> paths) :
        owner_pid(getpid()), paths(std::move(paths))
{}

discord_presence::~discord_presence()
{
	disconnect(getpid() == owner_pid);
}

void discord_presence::configure(bool enabled, std::string application_id)
{
	// An application ID is public, but must be an actual numeric Discord ID.
	enabled = enabled and not application_id.empty() and application_id.size() <= 20 and
	          std::ranges::all_of(application_id, [](char c) { return c >= '0' and c <= '9'; });
	if (this->enabled == enabled and this->application_id == application_id)
		return;
	disconnect(true);
	this->enabled = enabled;
	this->application_id = std::move(application_id);
	retry_at = {};
	retry_delay = std::chrono::seconds(1);
}

void discord_presence::set_connected(bool connected)
{
	if (headset_connected == connected)
		return;
	headset_connected = connected;
	application.clear();
	started = connected ? std::time(nullptr) : 0;
	dirty = true;
	if (not connected)
		disconnect(true);
}

void discord_presence::set_headset(std::string name)
{
	name = field(name);
	if (headset != name)
	{
		headset = std::move(name);
		dirty = true;
	}
}

void discord_presence::set_application(std::string name)
{
	name = field(name);
	if (application != name)
	{
		application = std::move(name);
		dirty = true;
	}
}

nlohmann::json discord_presence::activity() const
{
	return {
	        {"type", 0},
	        {"details", field(application.empty() ? "In VR lobby" : "Playing " + application)},
	        {"state", field("Streaming to " + (headset.empty() ? "VR headset" : headset))},
	        {"timestamps", {{"start", started}}},
	};
}

nlohmann::json discord_presence::command(nlohmann::json activity)
{
	return {
	        {"cmd", "SET_ACTIVITY"},
	        {"args", {{"pid", owner_pid}, {"activity", std::move(activity)}}},
	        {"nonce", std::to_string(++nonce)},
	};
}

bool discord_presence::queue(uint32_t opcode, std::string_view payload)
{
	if (payload.size() > max_payload or outgoing.size() + 8 + payload.size() > 2 * max_payload)
		return false;
	append_u32(outgoing, opcode);
	append_u32(outgoing, uint32_t(payload.size()));
	outgoing.insert(outgoing.end(), payload.begin(), payload.end());
	return true;
}

bool discord_presence::flush()
{
	while (written < outgoing.size())
	{
		auto count = send(fd, outgoing.data() + written, outgoing.size() - written, MSG_NOSIGNAL);
		if (count < 0 and errno == EINTR)
			continue;
		if (count < 0 and (errno == EAGAIN or errno == EWOULDBLOCK))
			return true;
		if (count <= 0)
			return false;
		written += count;
	}
	outgoing.clear();
	written = 0;
	return true;
}

void discord_presence::disconnect(bool clear_activity)
{
	if (fd >= 0)
	{
		if (clear_activity and getpid() == owner_pid and connection_state == state::ready)
		{
			queue(1, encode(command(nullptr)));
			flush();
		}
		close(fd);
	}
	fd = -1;
	connection_state = state::disconnected;
	incoming.clear();
	outgoing.clear();
	written = 0;
	dirty = true;
}

void discord_presence::after_fork_child()
{
	// Only the main process owns presence. A Monado child must not keep the
	// socket alive or clear the main process's activity on shutdown.
	disconnect(false);
}

void discord_presence::retry(clock::time_point now)
{
	disconnect();
	retry_at = now + retry_delay;
	retry_delay = std::min(retry_delay * 2, std::chrono::seconds(30));
}

void discord_presence::connect(clock::time_point now)
{
	for (const auto & path: paths)
	{
		sockaddr_un address{};
		const auto & name = path.native();
		if (name.size() >= sizeof(address.sun_path))
			continue;
		address.sun_family = AF_UNIX;
		std::memcpy(address.sun_path, name.c_str(), name.size() + 1);
		fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
		if (fd < 0)
			break;
		if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0)
		{
			connection_state = state::handshake;
			deadline = now + std::chrono::seconds(5);
			queue(0, encode({{"v", 1}, {"client_id", application_id}}));
			return;
		}
		if (errno == EINPROGRESS)
		{
			connection_state = state::connecting;
			deadline = now + std::chrono::seconds(2);
			return;
		}
		close(fd);
		fd = -1;
	}
	retry(now);
}

bool discord_presence::receive(clock::time_point now)
{
	std::array<uint8_t, 4096> buffer;
	// Bound the work per tick, even if the peer keeps supplying data.
	for (int reads = 0; reads < 16; ++reads)
	{
		auto count = recv(fd, buffer.data(), buffer.size(), 0);
		if (count < 0 and errno == EINTR)
			continue;
		if (count < 0 and (errno == EAGAIN or errno == EWOULDBLOCK))
			break;
		if (count <= 0 or incoming.size() + count > max_payload + 8)
			return false;
		incoming.insert(incoming.end(), buffer.begin(), buffer.begin() + count);
	}

	size_t consumed = 0;
	while (incoming.size() - consumed >= 8)
	{
		auto opcode = read_u32(incoming.data() + consumed);
		auto length = read_u32(incoming.data() + consumed + 4);
		if (length > max_payload)
			return false;
		if (incoming.size() - consumed - 8 < length)
			break;
		std::string_view payload(reinterpret_cast<const char *>(incoming.data() + consumed + 8), length);
		if (opcode == 3)
		{
			if (not queue(4, payload))
				return false;
		}
		else if (opcode == 1)
		{
			auto message = nlohmann::json::parse(payload, nullptr, false);
			if (not message.is_object())
				return false;
			auto event = message.find("evt");
			if (event != message.end() and *event == "ERROR")
			{
				retry_delay = std::chrono::seconds(30);
				return false;
			}
			if (event != message.end() and *event == "READY" and connection_state == state::handshake and message.value("cmd", nlohmann::json{}) == "DISPATCH")
			{
				connection_state = state::ready;
				retry_delay = std::chrono::seconds(1);
				next_update = now;
				dirty = true;
			}
		}
		else if (opcode != 4)
			return false;
		consumed += 8 + length;
	}
	incoming.erase(incoming.begin(), incoming.begin() + consumed);
	return true;
}

void discord_presence::poll(clock::time_point now)
{
	if (getpid() != owner_pid or not enabled or not headset_connected)
		return;
	if (fd < 0)
	{
		if (now < retry_at)
			return;
		connect(now);
		if (fd < 0)
			return;
	}
	if (connection_state == state::connecting)
	{
		pollfd socket{fd, POLLOUT, 0};
		if (::poll(&socket, 1, 0) <= 0)
		{
			if (now >= deadline)
				retry(now);
			return;
		}
		int error = 0;
		socklen_t length = sizeof(error);
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 or error)
		{
			retry(now);
			return;
		}
		connection_state = state::handshake;
		deadline = now + std::chrono::seconds(5);
		queue(0, encode({{"v", 1}, {"client_id", application_id}}));
	}
	if (not flush() or not receive(now))
	{
		retry(now);
		return;
	}
	if (connection_state == state::handshake and now >= deadline)
	{
		retry(now);
		return;
	}
	if (connection_state == state::ready and dirty and now >= next_update)
	{
		if (not queue(1, encode(command(activity()))))
		{
			retry(now);
			return;
		}
		dirty = false;
		next_update = now + std::chrono::seconds(15);
	}
	if (not flush())
		retry(now);
}
} // namespace wivrn
