// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "network.h"
#include "wyrm_serialization.h"
#include <array>
#include <memory>
#include <vector>

namespace wyrm
{
struct link_statistics
{
	network_endpoint local;
	network_endpoint remote;
	double rtt_ms = 0;
	double jitter_ms = 0;
	double loss = 0;
	double incoming_loss = 0;
	uint64_t received_video_packets = 0;
	uint64_t video_gaps = 0;
	uint64_t repairs_sent = 0;
	uint64_t recovered_fragments = 0;
	uint64_t capacity_bps = 0;
	uint64_t sent_bytes = 0;
	uint64_t received_bytes = 0;
	uint64_t sent_bps = 0;
	uint64_t received_bps = 0;
	bool active = false;
	bool ever_active = false;
};

struct multilink_options
{
	bool arrival_scheduling = true;
	uint8_t fec_group_size = 0;
	uint64_t aggregate_limit_bps = 0;
	bool shared_bottleneck = false;
	double loss_threshold = 0.02;
};

struct multilink_diagnostics
{
	size_t queued_bytes = 0;
	size_t reliable_messages = 0;
	size_t bulk_messages = 0;
	double oldest_control_ms = 0;
	uint64_t expired_video = 0;
	uint64_t rejected_bulk = 0;
	uint64_t rejected_video = 0;
	size_t protected_video_messages = 0;
	size_t receiving_assemblies = 0;
};

// Owns queued payloads. serialization_packet spans may reference encoder memory.
class multilink
{
	struct implementation;
	std::unique_ptr<implementation> impl;

public:
	using key_type = std::array<uint8_t, 32>;
	multilink();
	multilink(bool server, key_type key, std::vector<network_endpoint> local, std::vector<network_endpoint> remote = {}, bool refresh_interfaces = true);
	multilink(multilink &&) noexcept;
	multilink & operator=(multilink &&) noexcept;
	~multilink();
	static key_type random_key();
	explicit operator bool() const;
	int get_fd() const;
	std::vector<network_endpoint> endpoints() const;
	std::vector<link_statistics> statistics() const;
	uint64_t capacity_bps() const;
	multilink_diagnostics diagnostics() const;
	void set_options(multilink_options);
	void shutdown();
	void prepare_fork();
	void resume_after_fork();
	size_t send_raw(serialization_packet && packet);
	size_t send_reply_raw(serialization_packet && packet);
	size_t send_video_raw(serialization_packet && packet, std::chrono::steady_clock::time_point deadline);
	size_t send_video_raw(serialization_packet && packet, std::chrono::steady_clock::time_point deadline, bool protect);
	size_t send_reliable_raw(serialization_packet && packet);
	size_t send_bulk_raw(serialization_packet && packet);
	size_t send_many_raw(std::span<serialization_packet> packets);
	deserialization_packet receive_raw();
	deserialization_packet receive_pending();
};
} // namespace wyrm
