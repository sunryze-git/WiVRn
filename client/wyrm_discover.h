#pragma once

#include "wifi_lock.h"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <thread>
#include <variant>
#include <vector>

class dnssd_cache;

class wyrm_discover
{
public:
	struct service
	{
		struct address
		{
			std::string interface;
			std::variant<sockaddr_in, sockaddr_in6> address;
		};
		std::string name;
		std::string hostname;
		int port;
		bool tcp_only = false;

		std::vector<address> addresses;
		std::map<std::string, std::string> txt;
		std::chrono::steady_clock::time_point ttl;
		std::string pin;
	};

	static inline const std::chrono::milliseconds poll_min_time{500};
	static inline const std::chrono::milliseconds poll_max_time{10000};
	static inline const std::chrono::milliseconds discover_period{5000};

private:
	wifi_lock::multicast multicast;
	std::unique_ptr<dnssd_cache> cache;

	std::thread dnssd_thread;
	std::mutex mutex;
	std::vector<service> services;
	std::atomic<bool> quit = false;

	void discover(std::string service_name);

public:
	wyrm_discover(std::string service_name = "_wyrm._tcp.local.");
	wyrm_discover(const wyrm_discover &) = delete;
	wyrm_discover(wyrm_discover &&) = delete;
	wyrm_discover operator=(const wyrm_discover &) = delete;
	wyrm_discover operator=(wyrm_discover &&) = delete;
	~wyrm_discover();

	std::vector<service> get_services();
};
