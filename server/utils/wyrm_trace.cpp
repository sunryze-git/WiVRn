/*
 * Wyrm VR streaming
 * Copyright (C) 2026  Wyrm Contributors
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

#include "wyrm_trace.h"

#include "multilink.h"

#include "util/u_trace_marker.h"

#ifdef WYRM_USE_PERFETTO

#include "os/os_time.h"
#include "util/u_debug.h"
#include "util/u_logging.h"
#include "utils/xdg_base_directory.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <perfetto.h>
#include <string>
#include <unistd.h>
#include <vector>

PERFETTO_DEFINE_CATEGORIES(
        perfetto::Category("wyrm_encoder").SetDescription("Wyrm encoder CPU spans"),
        perfetto::Category("wyrm_encoder_gpu").SetDescription("Wyrm encoder GPU slices"),
        perfetto::Category("wyrm_compositor").SetDescription("Wyrm compositor spans"),
        perfetto::Category("wyrm_network").SetDescription("Wyrm network send spans"),
        perfetto::Category("wyrm_feedback").SetDescription("Wyrm dump_time markers"));
PERFETTO_TRACK_EVENT_STATIC_STORAGE();

DEBUG_GET_ONCE_OPTION(wyrm_tracing, "WYRM_TRACING", NULL)

namespace wyrm::trace
{
namespace
{
bool initialized = false;
std::atomic<bool> enabled = false;
::vk_bundle * calibration = nullptr;

perfetto::Track g_feedback_track(4);

constexpr std::array cpu_track_names{"Wyrm encoder CPU", "Wyrm compositor CPU", "Wyrm network CPU"};
constexpr std::array gpu_track_names{"Wyrm Vulkan encode", "Wyrm Vulkan image copy", "Wyrm Vulkan host copy", "Wyrm Vulkan host copy overflow", "Wyrm NVENC copy", "Wyrm VA copy"};

void set_track_name(const perfetto::Track & track, const char * name)
{
	auto desc = track.Serialize();
	desc.set_name(name);
	perfetto::TrackEvent::SetTrackDescriptor(track, desc);
}

perfetto::Track frame_cpu_track_obj(cpu_track which, uint8_t stream)
{
	return perfetto::Track(0x1000 + uint64_t(which) * 256 + stream);
}

perfetto::Track gpu_track_obj(gpu_track which, uint8_t stream)
{
	return perfetto::Track(0x2000 + uint64_t(which) * 256 + stream);
}

// WYRM_TRACING is the single switch: it both enables tracing and selects which Perfetto
// backend(s) init() arms.
//   unset/off/0/false/no - disabled
//   system / 1/true/yes/on - connect to an external traced as a producer (the consumer
//                            owns the session); legacy boolean values map here
//   inprocess             - run the tracing service in this process and write the file
//   system+inprocess      - arm both independently
// Unknown values emit a warning and leave tracing disabled.
enum class tracing_mode
{
	off,
	system,
	inprocess,
	both,
};

// In-process session state (only used when the mode includes inprocess). The session
// must outlive every emit and be stopped/flushed in shutdown() to produce a file.
std::unique_ptr<perfetto::TracingSession> g_inproc_session;

tracing_mode parse_tracing_mode()
{
	const char * v = debug_get_option_wyrm_tracing();
	if (!v or !*v)
		return tracing_mode::off;
	const std::string s = v;
	if (s == "off" or s == "0" or s == "false" or s == "no")
		return tracing_mode::off;
	if (s == "system" or s == "1" or s == "true" or s == "yes" or s == "on")
		return tracing_mode::system;
	if (s == "inprocess")
		return tracing_mode::inprocess;
	if (s == "system+inprocess" or s == "both")
		return tracing_mode::both;
	U_LOG_W("wyrm::trace: unknown WYRM_TRACING '%s' (expected off|system|inprocess|system+inprocess), tracing disabled", v);
	return tracing_mode::off;
}

// Default socket is root-only; use a per-user XDG_RUNTIME_DIR path so a user-launched traced can bind.
std::string default_producer_sock_path()
{
	return (xdg_runtime_dir() / "wyrm-perfetto-producer.sock").string();
}

// Local-time stamp for trace filenames, evaluated per flush.
std::string now_timestamp()
{
	const std::time_t now = std::time(nullptr);
	std::tm tm{};
	localtime_r(&now, &tm);
	char buf[16];
	std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
	return buf;
}

std::string expand_file_placeholders(std::string s)
{
	const std::string ts = now_timestamp();
	const std::string pid = std::to_string(getpid());
	for (size_t pos = 0; (pos = s.find('%', pos)) != std::string::npos;)
	{
		if (pos + 1 >= s.size())
			break;
		const char c = s[pos + 1];
		const std::string * rep = nullptr;
		if (c == 't')
			rep = &ts;
		else if (c == 'p')
			rep = &pid;
		if (rep)
		{
			s.replace(pos, 2, *rep);
			pos += rep->size();
		}
		else
			pos += 2; // leave any other %x untouched
	}
	return s;
}

// Path from WYRM_TRACING_FILE; unset ⇒ wyrm-<ts>-<pid>.pftrace in cwd. %t/%p expanded.
std::string resolve_inproc_file_path()
{
	const char * file = std::getenv("WYRM_TRACING_FILE");
	if (!file or !*file)
		return "wyrm-" + now_timestamp() + "-" + std::to_string(getpid()) + ".pftrace";
	return expand_file_placeholders(file);
}

// Don't clobber an existing trace; suffix -2, -3, ... until free.
std::filesystem::path unique_file_path(const std::string & candidate)
{
	std::filesystem::path p = candidate;
	std::error_code ec;
	if (not std::filesystem::exists(p, ec))
		return p;
	const auto dir = p.parent_path();
	const auto stem = p.stem().string();
	const auto ext = p.extension().string();
	for (unsigned n = 2;; ++n)
	{
		std::filesystem::path q = dir / (stem + "-" + std::to_string(n) + ext);
		if (not std::filesystem::exists(q, ec))
			return q;
	}
}

// Flush + stop the session and write its buffer; caller re-arms or resets.
void write_inproc_session()
{
	perfetto::TrackEvent::Flush();
	g_inproc_session->StopBlocking();
	const std::vector<char> data = g_inproc_session->ReadTraceBlocking();

	const std::filesystem::path path = unique_file_path(resolve_inproc_file_path());
	std::error_code ec;
	if (const auto parent = path.parent_path(); not parent.empty())
		std::filesystem::create_directories(parent, ec);

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (out)
	{
		out.write(data.data(), static_cast<std::streamsize>(data.size()));
		U_LOG_I("wyrm::trace: wrote %zu bytes to %s", data.size(), path.string().c_str());
	}
	else
		U_LOG_E("wyrm::trace: failed to open %s for writing", path.string().c_str());
}

// Arm an in-process tracing session. The output path is resolved when the buffer is written
// (on flush_session/shutdown), so each rotated trace gets its own timestamped file.
void start_inprocess_session()
{
	uint32_t buffer_kb = 65536;
	if (const char * b = std::getenv("WYRM_TRACING_BUFFER_KB"); b and *b)
	{
		if (const long parsed = std::strtol(b, nullptr, 10); parsed > 0)
			buffer_kb = static_cast<uint32_t>(parsed);
	}

	// Enable only the Wyrm categories, mirroring the system-backend .pbtx allowlist.
	// Do NOT add disabled_categories("*"): per wyrm_trace_cfg.pbtx that silences the
	// allowlist too. wyrm-server defines only these categories, so this fully scopes it.
	perfetto::protos::gen::TrackEventConfig te;
	te.add_enabled_categories("wyrm_encoder");
	te.add_enabled_categories("wyrm_encoder_gpu");
	te.add_enabled_categories("wyrm_compositor");
	te.add_enabled_categories("wyrm_network");
	te.add_enabled_categories("wyrm_feedback");

	perfetto::TraceConfig cfg;
	cfg.add_buffers()->set_size_kb(buffer_kb);
	auto * ds = cfg.add_data_sources()->mutable_config();
	ds->set_name("track_event");
	ds->set_track_event_config_raw(te.SerializeAsString());

	g_inproc_session = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
	g_inproc_session->Setup(cfg);
	g_inproc_session->StartBlocking();
	U_LOG_I("wyrm::trace: in-process session started, %u KiB buffer", buffer_kb);
}

perfetto::Track & cpu_track_obj(cpu_track d)
{
	static_assert(sizeof(perfetto::base::PlatformThreadId) <= sizeof(uint32_t));
	struct thread_tracks
	{
		perfetto::ThreadTrack thread = perfetto::ThreadTrack::Current();
		// Track IDs XOR their parent, whose thread ID occupies only the low 32 bits.
		std::array<perfetto::Track, cpu_track_names.size()> tracks{
		        perfetto::Track(uint64_t(1) << 32, thread),
		        perfetto::Track(uint64_t(2) << 32, thread),
		        perfetto::Track(uint64_t(3) << 32, thread),
		};
		thread_tracks()
		{
			perfetto::TrackEvent::SetTrackDescriptor(thread, thread.Serialize());
			for (size_t i = 0; i < tracks.size(); ++i)
				set_track_name(tracks[i], cpu_track_names[i]);
		}
	};
	thread_local thread_tracks local;
	return local.tracks[size_t(d)];
}
} // namespace

// Bridge the WYRM_TRACING switch onto Monado's tracing before u_trace_marker_init() caches
// XRT_TRACING. Only the system backend reaches a traced consumer, so only enable Monado then;
// setenv(..., 0) preserves an explicit user-set XRT_TRACING. No-op without WYRM_TRACE_MONADO.
static void propagate_to_monado()
{
#ifdef WYRM_TRACE_MONADO
	const tracing_mode mode = parse_tracing_mode();
	if (mode == tracing_mode::system or mode == tracing_mode::both)
		setenv("XRT_TRACING", "true", 0);
#endif
}

void init()
{
	if (initialized)
		return;
	initialized = true;

	// Monado's marker init caches XRT_TRACING, so propagate_to_monado() must run first.
	// Both run regardless of WYRM_TRACING so Monado's tracing behaves as it would normally.
	propagate_to_monado();
	u_trace_marker_init();

	const tracing_mode mode = parse_tracing_mode();
	if (mode == tracing_mode::off)
	{
		U_LOG_D("wyrm::trace: WYRM_TRACING not set, perfetto tracing disabled");
		return;
	}
	const bool use_system = mode == tracing_mode::system or mode == tracing_mode::both;
	const bool use_inprocess = mode == tracing_mode::inprocess or mode == tracing_mode::both;

	perfetto::TracingInitArgs args;
	args.use_monotonic_clock = true;
	if (use_system)
	{
		const auto sock = default_producer_sock_path();
		if (setenv("PERFETTO_PRODUCER_SOCK_NAME", sock.c_str(), 0) == 0)
			U_LOG_I("wyrm::trace: producer socket %s", std::getenv("PERFETTO_PRODUCER_SOCK_NAME"));
		args.backends |= perfetto::kSystemBackend;
	}
	if (use_inprocess)
		args.backends |= perfetto::kInProcessBackend;
	perfetto::Tracing::Initialize(args);
	perfetto::TrackEvent::Register();

	set_track_name(g_feedback_track, "Wyrm feedback");
	for (uint8_t stream = 0; stream < 3; ++stream)
	{
		for (size_t i = 0; i < cpu_track_names.size(); ++i)
		{
			const auto name = std::string(cpu_track_names[i]) + " stream " + std::to_string(stream);
			set_track_name(frame_cpu_track_obj(cpu_track(i), stream), name.c_str());
		}
		for (size_t i = 0; i < gpu_track_names.size(); ++i)
		{
			const auto name = std::string(gpu_track_names[i]) + " stream " + std::to_string(stream);
			set_track_name(gpu_track_obj(gpu_track(i), stream), name.c_str());
		}
	}

	// The in-process session must be started after Register() so its data source is known.
	if (use_inprocess)
		start_inprocess_session();

	enabled.store(true, std::memory_order_release);
	U_LOG_I("wyrm::trace: init complete");
}

void flush_session()
{
	if (!initialized or !g_inproc_session)
		return;
	// Client gone but process lives; flush and re-arm for the next one.
	write_inproc_session();
	g_inproc_session.reset();
	start_inprocess_session();
}

void shutdown()
{
	if (!initialized or !g_inproc_session)
		return;
	write_inproc_session();
	g_inproc_session.reset();
}

bool is_enabled()
{
	return enabled.load(std::memory_order_acquire);
}

void set_calibration_source(::vk_bundle * monado_vk)
{
	calibration = monado_vk;
}

::vk_bundle * calibration_source()
{
	return calibration;
}

void instant_feedback(const char * name, int64_t time_ns, uint64_t frame, uint8_t stream)
{
	if (!initialized)
		return;
	TRACE_EVENT_INSTANT("wyrm_feedback", perfetto::DynamicString(name), g_feedback_track, static_cast<uint64_t>(time_ns), "frame", frame, "stream", static_cast<uint32_t>(stream));
}

void video_queue_dropped(const video_queue_drop & drop, uint32_t stream, uint32_t generation, uint64_t frame, uint16_t shard)
{
	if (!enabled.load(std::memory_order_acquire))
		return;
	const char * name = nullptr;
	switch (drop.reason)
	{
		case video_queue_drop_reason::deadline:
			name = "drop_video_deadline";
			break;
		case video_queue_drop_reason::arrival_impossible:
			name = "drop_video_arrival_impossible";
			break;
		case video_queue_drop_reason::queue_full:
			name = "drop_video_queue_full";
			break;
	}
	const auto ns = [](auto time) { return std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count(); };
	TRACE_EVENT_INSTANT("wyrm_network", perfetto::DynamicString(name), g_feedback_track, static_cast<uint64_t>(ns(drop.dropped_at)),
	                    "frame", frame, "stream", stream, "generation", generation, "shard", shard,
	                    "enqueued_ns", ns(drop.enqueued_at), "deadline_ns", ns(drop.deadline), "target_deadline_ns", ns(drop.target_deadline),
	                    "fragment_cursor", drop.fragment_cursor, "queued_bytes", drop.queued_bytes);
}

void gpu_slice(gpu_track which, const char * slice_name, int64_t begin_ns, int64_t end_ns, uint64_t frame, uint8_t stream)
{
	if (!initialized)
		return;
	const auto track = gpu_track_obj(which, stream);
	TRACE_EVENT_BEGIN("wyrm_encoder_gpu", perfetto::DynamicString(slice_name), track, static_cast<uint64_t>(begin_ns), "frame", frame, "stream", static_cast<uint32_t>(stream));
	TRACE_EVENT_END("wyrm_encoder_gpu", track, static_cast<uint64_t>(end_ns));
}

#define WYRM_CPU_BEGIN(CAT, trk, name, ts, frame, stream) \
	TRACE_EVENT_BEGIN(CAT, perfetto::DynamicString(name), (trk), static_cast<uint64_t>(ts), "frame", (frame), "stream", static_cast<uint32_t>(stream))

scope::scope(cpu_track which, uint8_t stream, uint64_t frame, const char * name) :
        which(which), stream(stream), frame(frame), name(name), begin_ns(0), active(false)
{
	if (!initialized)
		return;
	bool enabled = false;
	switch (which)
	{
		case cpu_track::encoder:
			enabled = TRACE_EVENT_CATEGORY_ENABLED("wyrm_encoder");
			break;
		case cpu_track::compositor:
			enabled = TRACE_EVENT_CATEGORY_ENABLED("wyrm_compositor");
			break;
		case cpu_track::network:
			enabled = TRACE_EVENT_CATEGORY_ENABLED("wyrm_network");
			break;
	}
	if (!enabled)
		return;
	active = true;
	begin_ns = os_monotonic_get_ns();
}

scope::~scope()
{
	if (!active)
		return;
	const int64_t end_ns = os_monotonic_get_ns();
	perfetto::Track & trk = cpu_track_obj(which);
	switch (which)
	{
		case cpu_track::encoder:
			WYRM_CPU_BEGIN("wyrm_encoder", trk, name, begin_ns, frame, stream);
			TRACE_EVENT_END("wyrm_encoder", trk, static_cast<uint64_t>(end_ns));
			break;
		case cpu_track::compositor:
			WYRM_CPU_BEGIN("wyrm_compositor", trk, name, begin_ns, frame, stream);
			TRACE_EVENT_END("wyrm_compositor", trk, static_cast<uint64_t>(end_ns));
			break;
		case cpu_track::network:
			WYRM_CPU_BEGIN("wyrm_network", trk, name, begin_ns, frame, stream);
			TRACE_EVENT_END("wyrm_network", trk, static_cast<uint64_t>(end_ns));
			break;
	}
}

void cpu_instant(cpu_track which, const char * name, uint64_t frame, uint8_t stream)
{
	if (!enabled.load(std::memory_order_acquire))
		return;
	const int64_t ts = os_monotonic_get_ns();
	perfetto::Track & trk = cpu_track_obj(which);
	switch (which)
	{
		case cpu_track::encoder:
			TRACE_EVENT_INSTANT("wyrm_encoder", perfetto::DynamicString(name), trk, static_cast<uint64_t>(ts), "frame", frame, "stream", static_cast<uint32_t>(stream));
			break;
		case cpu_track::compositor:
			TRACE_EVENT_INSTANT("wyrm_compositor", perfetto::DynamicString(name), trk, static_cast<uint64_t>(ts), "frame", frame, "stream", static_cast<uint32_t>(stream));
			break;
		case cpu_track::network:
			TRACE_EVENT_INSTANT("wyrm_network", perfetto::DynamicString(name), trk, static_cast<uint64_t>(ts), "frame", frame, "stream", static_cast<uint32_t>(stream));
			break;
	}
}

void cpu_begin(cpu_track which, uint8_t stream, uint64_t frame, const char * name)
{
	if (!initialized)
		return;
	const int64_t ts = os_monotonic_get_ns();
	// A frame can begin with headers on the encoder thread and finish on the sender.
	const auto trk = frame_cpu_track_obj(which, stream);
	switch (which)
	{
		case cpu_track::encoder:
			WYRM_CPU_BEGIN("wyrm_encoder", trk, name, ts, frame, stream);
			break;
		case cpu_track::compositor:
			WYRM_CPU_BEGIN("wyrm_compositor", trk, name, ts, frame, stream);
			break;
		case cpu_track::network:
			WYRM_CPU_BEGIN("wyrm_network", trk, name, ts, frame, stream);
			break;
	}
}

void cpu_end(cpu_track which, uint8_t stream, uint64_t, const char *)
{
	if (!initialized)
		return;
	const int64_t ts = os_monotonic_get_ns();
	const auto trk = frame_cpu_track_obj(which, stream);
	switch (which)
	{
		case cpu_track::encoder:
			TRACE_EVENT_END("wyrm_encoder", trk, static_cast<uint64_t>(ts));
			break;
		case cpu_track::compositor:
			TRACE_EVENT_END("wyrm_compositor", trk, static_cast<uint64_t>(ts));
			break;
		case cpu_track::network:
			TRACE_EVENT_END("wyrm_network", trk, static_cast<uint64_t>(ts));
			break;
	}
}
} // namespace wyrm::trace

#else // !WYRM_USE_PERFETTO

namespace wyrm::trace
{
void init()
{
	u_trace_marker_init();
}
void flush_session() {}
void shutdown() {}
bool is_enabled()
{
	return false;
}
void set_calibration_source(::vk_bundle *) {}
::vk_bundle * calibration_source()
{
	return nullptr;
}
void instant_feedback(const char *, int64_t, uint64_t, uint8_t) {}
void video_queue_dropped(const video_queue_drop &, uint32_t, uint32_t, uint64_t, uint16_t) {}
void gpu_slice(gpu_track, const char *, int64_t, int64_t, uint64_t, uint8_t) {}
scope::scope(cpu_track which, uint8_t stream, uint64_t frame, const char * name) :
        which(which), stream(stream), frame(frame), name(name), begin_ns(0), active(false) {}
scope::~scope() {}
void cpu_instant(cpu_track, const char *, uint64_t, uint8_t) {}
void cpu_begin(cpu_track, uint8_t, uint64_t, const char *) {}
void cpu_end(cpu_track, uint8_t, uint64_t, const char *) {}
} // namespace wyrm::trace

#endif // WYRM_USE_PERFETTO
