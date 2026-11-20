// SPDX-License-Identifier: GPL-3.0-or-later
#include "discord_presence.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <source_location>
#include <span>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std::chrono_literals;
using wivrn::discord_presence;
using json = nlohmann::json;
static constexpr auto application_id = "123456789012345678";

static void check(bool valid, std::source_location where = std::source_location::current())
{
	if (not valid)
	{
		std::cerr << where.file_name() << ':' << where.line() << '\n';
		std::abort();
	}
}

struct fake_discord
{
	std::filesystem::path directory, path;
	int listener = -1, peer = -1;
	std::vector<uint8_t> received;

	fake_discord()
	{
		char temp[] = "/tmp/wivrn-rpc-test-XXXXXX";
		check(mkdtemp(temp) != nullptr);
		directory = temp;
		path = directory / "discord-ipc-0";
	}
	void listen()
	{
		listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
		check(listener >= 0);
		sockaddr_un address{};
		address.sun_family = AF_UNIX;
		std::strcpy(address.sun_path, path.c_str());
		check(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
		check(::listen(listener, 4) == 0);
	}
	void accept()
	{
		peer = accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
		check(peer >= 0);
		received.clear();
	}
	static std::vector<uint8_t> frame(uint32_t opcode, std::string_view payload)
	{
		std::vector<uint8_t> bytes;
		for (uint32_t value: {opcode, uint32_t(payload.size())})
			for (int byte = 0; byte < 4; ++byte)
				bytes.push_back(uint8_t(value >> (8 * byte)));
		bytes.insert(bytes.end(), payload.begin(), payload.end());
		return bytes;
	}
	void send(std::span<const uint8_t> bytes)
	{
		check(::send(peer, bytes.data(), bytes.size(), MSG_NOSIGNAL) == ssize_t(bytes.size()));
	}
	void send(uint32_t opcode, std::string_view payload)
	{
		send(frame(opcode, payload));
	}
	std::pair<uint32_t, std::string> read()
	{
		std::array<uint8_t, 8192> bytes;
		for (;;)
		{
			auto count = recv(peer, bytes.data(), bytes.size(), MSG_DONTWAIT);
			if (count <= 0)
				break;
			received.insert(received.end(), bytes.begin(), bytes.begin() + count);
		}
		check(received.size() >= 8);
		uint32_t opcode = 0, length = 0;
		for (int byte = 0; byte < 4; ++byte)
		{
			opcode |= uint32_t(received[byte]) << (8 * byte);
			length |= uint32_t(received[byte + 4]) << (8 * byte);
		}
		check(received.size() >= 8 + length);
		std::string payload(received.begin() + 8, received.begin() + 8 + length);
		received.erase(received.begin(), received.begin() + 8 + length);
		return {opcode, std::move(payload)};
	}
	bool no_message()
	{
		char byte;
		return received.empty() and recv(peer, &byte, 1, MSG_DONTWAIT) < 0 and errno == EAGAIN;
	}
	void ready(discord_presence & presence, discord_presence::clock::time_point now)
	{
		auto [opcode, payload] = read();
		check(opcode == 0 and json::parse(payload) == json({{"v", 1}, {"client_id", application_id}}));
		send(1, R"({"cmd":"DISPATCH","evt":"READY"})");
		presence.poll(now);
	}
	json activity()
	{
		auto [opcode, payload] = read();
		check(opcode == 1);
		auto message = json::parse(payload);
		check(message["cmd"] == "SET_ACTIVITY" and message["args"]["pid"] == getpid() and message["nonce"].is_string());
		return message["args"]["activity"];
	}
	~fake_discord()
	{
		if (peer >= 0)
			close(peer);
		if (listener >= 0)
			close(listener);
		std::filesystem::remove_all(directory);
	}
};

static void test_session()
{
	fake_discord discord;
	discord.listen();
	discord_presence presence({discord.path});
	presence.configure(true, application_id);
	presence.set_connected(true);
	presence.set_headset("Steam Frame");
	auto now = discord_presence::clock::now();
	presence.poll(now);
	discord.accept();
	auto handshake = discord.read();
	check(handshake.first == 0 and json::parse(handshake.second)["client_id"] == application_id);
	// Fragmented headers and payloads must wait for a complete READY frame.
	auto ready = fake_discord::frame(1, R"({"cmd":"DISPATCH","evt":"READY"})");
	discord.send(std::span(ready).first(3));
	presence.poll(now);
	check(discord.no_message());
	discord.send(std::span(ready).subspan(3, 8));
	presence.poll(now);
	check(discord.no_message());
	discord.send(std::span(ready).subspan(11));
	presence.poll(now);
	auto lobby = discord.activity();
	check(lobby["details"] == "In VR lobby" and lobby["state"] == "Streaming to Steam Frame");
	check(lobby["timestamps"]["start"].get<int64_t>() > 0);
	// Game changes coalesce, preserving the session timestamp.
	presence.set_application("Old game");
	presence.set_application("Beat Saber");
	presence.poll(now + 1s);
	check(discord.no_message());
	presence.poll(now + 16s);
	auto game = discord.activity();
	check(game["details"] == "Playing Beat Saber" and game["timestamps"] == lobby["timestamps"]);
	discord.send(3, "keepalive");
	discord.send(1, R"({"cmd":"SET_ACTIVITY","evt":null,"nonce":"1","data":null})");
	presence.poll(now + 17s);
	check(discord.read() == std::pair<uint32_t, std::string>(4, "keepalive"));
	presence.set_connected(false);
	check(discord.activity().is_null());
}

static void test_reconnect_and_opt_out()
{
	fake_discord discord;
	discord_presence presence({discord.path});
	presence.configure(true, application_id);
	presence.set_connected(true);
	auto now = discord_presence::clock::now();
	presence.poll(now); // Discord is not running yet.
	discord.listen();
	presence.poll(now + 2s);
	discord.accept();
	discord.ready(presence, now + 2s);
	auto first = discord.activity();
	close(discord.peer);
	discord.peer = -1;
	presence.set_application("Beat Saber");
	presence.poll(now + 3s);
	presence.poll(now + 5s);
	discord.accept();
	discord.ready(presence, now + 5s);
	auto second = discord.activity();
	check(second["details"] == "Playing Beat Saber" and second["timestamps"] == first["timestamps"]);
	presence.configure(false, application_id);
	check(discord.activity().is_null());
	presence.poll(now + 60s);
	check(accept4(discord.listener, nullptr, nullptr, SOCK_NONBLOCK) < 0 and errno == EAGAIN);
}

static void test_bad_peer()
{
	for (auto payload: {"not JSON", "[]", R"({"evt":"ERROR"})"})
	{
		fake_discord discord;
		discord.listen();
		discord_presence presence({discord.path});
		presence.configure(true, application_id);
		presence.set_connected(true);
		auto now = discord_presence::clock::now();
		presence.poll(now);
		discord.accept();
		discord.read();
		discord.send(1, payload);
		presence.poll(now);
		char byte;
		check(recv(discord.peer, &byte, 1, MSG_DONTWAIT) == 0);
	}
	for (bool oversized: {false, true})
	{
		fake_discord discord;
		discord.listen();
		discord_presence presence({discord.path});
		presence.configure(true, application_id);
		presence.set_connected(true);
		auto now = discord_presence::clock::now();
		presence.poll(now);
		discord.accept();
		discord.read();
		if (oversized)
		{
			const std::array<uint8_t, 8> header{1, 0, 0, 0, 0xff, 0xff, 0xff, 0x7f};
			discord.send(header);
		}
		presence.poll(now + 6s); // Also covers a peer that never sends READY.
		char byte;
		check(recv(discord.peer, &byte, 1, MSG_DONTWAIT) == 0);
	}
}

static void test_configuration_and_text()
{
	fake_discord discord;
	discord.listen();
	discord_presence presence({discord.path});
	auto now = discord_presence::clock::now();
	for (const char * id: {"", "not-an-id", "123456789012345678901"})
	{
		presence.configure(true, id);
		presence.set_connected(true);
		presence.poll(now);
		check(accept4(discord.listener, nullptr, nullptr, SOCK_NONBLOCK) < 0 and errno == EAGAIN);
	}
	presence.set_connected(false);
	presence.configure(true, application_id);
	presence.poll(now); // A configured but idle server must not advertise activity.
	check(accept4(discord.listener, nullptr, nullptr, SOCK_NONBLOCK) < 0 and errno == EAGAIN);
	presence.set_connected(true);
	presence.set_application(std::string(118, 'x') + "\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93");
	presence.set_headset("Steam\nFrame");
	presence.poll(now);
	discord.accept();
	discord.ready(presence, now);
	auto activity = discord.activity();
	check(activity["state"] == "Streaming to Steam Frame");
	check(activity["details"] == "Playing " + std::string(118, 'x'));
	presence.configure(true, "");
	check(discord.activity().is_null());
}

static void test_fork_and_destruction()
{
	fake_discord discord;
	discord.listen();
	auto presence = std::make_unique<discord_presence>(std::vector<std::filesystem::path>{discord.path});
	presence->configure(true, application_id);
	presence->set_connected(true);
	auto now = discord_presence::clock::now();
	presence->poll(now);
	discord.accept();
	discord.ready(*presence, now);
	discord.activity();
	pid_t child = fork();
	check(child >= 0);
	if (child == 0)
	{
		presence->after_fork_child();
		presence.reset();
		_exit(0);
	}
	int status;
	check(waitpid(child, &status, 0) == child and WIFEXITED(status) and WEXITSTATUS(status) == 0);
	check(discord.no_message());
	presence.reset();
	check(discord.activity().is_null());
}

int main()
{
	test_session();
	test_reconnect_and_opt_out();
	test_bad_peer();
	test_configuration_and_text();
	test_fork_and_destruction();
	std::cout << "Discord IPC fragmentation, activity, reconnect, opt-out, malformed peers and fork cleanup passed\n";
}
