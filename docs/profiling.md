# Profiling Wyrm

Performance analysis for Wyrm: [Perfetto](https://perfetto.dev/) tracing.
For general debugging see [docs/debugging.md](debugging.md).

## Quick start

Build with tracing (`server-tracing` preset = `server` + `WYRM_USE_PERFETTO=ON`):
```bash
cmake --preset server-tracing
cmake --build build-server-tracing
```

Self-contained **in-process** trace (no daemon, no script):
```bash
WYRM_TRACING=inprocess build-server-tracing/server/wyrm-server
```
Connect the HMD, stream, disconnect. Each session writes a `wyrm-<timestamp>-<pid>.pftrace` in cwd;
open it in <https://ui.perfetto.dev>.

**System-backend** capture via the daemon + wrapper:
```bash
WYRM_TRACING=system wyrm-server     # one shell, with a client streaming
tools/perfetto/wyrm_capture.py       # Wyrm-only; --full adds Monado + ftrace
```

`WYRM_USE_PERFETTO` defaults `OFF`; even built `ON`, tracing is inert until `WYRM_TRACING` is set.
The Perfetto amalgamated SDK must be installed where CMake looks (`/usr/share/perfetto/sdk`, override
with `-DPERFETTO_SDK_DIR=<dir>`); no automatic fetch.

## HMD profiling

```bash
adb shell setprop persist.traced.enable 1
```
Then use the [Perfetto UI](https://ui.perfetto.dev/) → connect device. See the
[Android tracing quickstart](https://perfetto.dev/docs/quickstart/android-tracing).

## Backends — `WYRM_TRACING`

`WYRM_TRACING` enables tracing and selects how it is collected. Unset/`off` = disabled; unknown
values warn and stay off.

| Value              | Behavior |
| ------------------ | -------- |
| `system`           | Connect to an external `traced` daemon; an external consumer owns the session. The only mode that gives a unified system-wide capture. Use `tools/perfetto/wyrm_capture.py`. |
| `inprocess`        | wyrm-server runs the service itself and writes a self-contained `.pftrace`. No daemon, no sudo. Wyrm track events only — no ftrace/system data. |
| `system+inprocess` | Both, independently → two separate traces (not merged). |

In-process options:
- `WYRM_TRACING_FILE` — output path. A trace is written on **each client disconnect** and at server
  shutdown, so one run can produce several files. Unset → timestamped `wyrm-<timestamp>-<pid>.pftrace`
  in cwd. An explicit value expands `%t` (timestamp)/`%p` (pid) and gains a `-2`, `-3`, … suffix
  rather than overwriting. Written from the per-session child, so use an absolute path if unsure;
  parent dirs are created.
- `WYRM_TRACING_BUFFER_KB` — ring buffer size in KiB (default `65536`).

A headset must be streaming during the capture window. Confirm setup by raising Monado's log level
(default `warn` hides the lines):
```bash
XRT_LOG=info WYRM_TRACING=system wyrm-server
# → wyrm::trace: producer socket /run/user/<uid>/wyrm-perfetto-producer.sock
# → wyrm::trace: init complete
```

> Perfetto's default socket is root-only, so Wyrm (and the wrapper) use
> `$XDG_RUNTIME_DIR/wyrm-perfetto-{producer,consumer}.sock` (fallback `/tmp`). Override via
> `PERFETTO_PRODUCER_SOCK_NAME`.


## Capturing — `wyrm_capture.py`

A wrapper around `tracebox` (downloaded on first run): starts the tracing services, waits for the
wyrm-server producer, captures, and tears down. If no producer connects within 20s it **aborts**
rather than write an empty trace — start wyrm-server with `WYRM_TRACING=system`, stream, retry.

```bash
tools/perfetto/wyrm_capture.py                       # default: Wyrm-only, no ftrace, no sudo
tools/perfetto/wyrm_capture.py --full                # + every producer + broad ftrace (needs sudo)
tools/perfetto/wyrm_capture.py --full --cpu-only     # full producers, ftrace stripped (no sudo)
tools/perfetto/wyrm_capture.py --duration-ms 5000 -o my.pftrace
tools/perfetto/wyrm_capture.py -c custom.pbtx        # any Perfetto config
```

`--cpu-only` strips any `linux.ftrace` block from the config. The wrapper auto-detects ftrace in the
config and only then starts `traced_probes` + uses `sudo`.

### Default vs `--full`

| Aspect | default (`wyrm_trace_cfg.pbtx`) | `--full` (`wyrm_full_trace_cfg.pbtx`) |
| ------ | -------------------------------- | -------------------------------------- |
| track_event | Wyrm categories only       | every category, every producer |
| ftrace | none                             | sched + freq + idle + IRQs + vblank |
| Buffers | 64 MiB (track_event)            | 64 MiB track_event **+** 256 MiB system |
| sudo   | no                               | yes (ftrace) |
| Use for | encoder-pipeline questions      | "something outside Wyrm is doing X" |

`--full` isolates `track_event` in its own buffer so the high-rate ftrace stream can't evict the
Wyrm/Monado slices, and defaults to a 5 s window (override with `--duration-ms`). If a trace is too
big to load, drop the `irq/*` events from a copy of the config.

### Manual capture

Drive Perfetto yourself against Wyrm's default socket pair:
```bash
SOCK_DIR="${XDG_RUNTIME_DIR:-/tmp}"
export PERFETTO_PRODUCER_SOCK_NAME="$SOCK_DIR/wyrm-perfetto-producer.sock"
export PERFETTO_CONSUMER_SOCK_NAME="$SOCK_DIR/wyrm-perfetto-consumer.sock"

traced --background                                    # add sudo + traced_probes for ftrace configs
WYRM_TRACING=system wyrm-server                      # in another shell, streaming
perfetto -c tools/perfetto/wyrm_trace_cfg.pbtx --txt -o trace.pftrace
```
For ftrace configs, run `traced`/`traced_probes` under `sudo -E` and add
`--set-socket-permissions "$USER:0660:$USER:0660"` to `traced`.

### Summaries

`tools/perfetto/pftrace_summary.py` prints per-slice stats, or a side-by-side diff for two traces:
```bash
tools/perfetto/pftrace_summary.py nvenc.pftrace                 # one trace
tools/perfetto/pftrace_summary.py nvenc.pftrace vulkan.pftrace  # diff with Δmean
```

## Tracing Monado too

Monado's own `u_trace` (built on percetto) can join the same session. Build with
`WYRM_TRACE_MONADO=ON` (requires `WYRM_USE_PERFETTO=ON`):
```bash
cmake --preset server-tracing-monado
cmake --build build-server-tracing-monado

WYRM_TRACING=system wyrm-server     # also sets Monado's XRT_TRACING for you
tools/perfetto/wyrm_capture.py --full
```

- **System backend + `--full` only.** Monado's percetto can't write in-process, and the default
  config allowlists only `wyrm_*` categories.
- **percetto must be available** at configure time (pkg-config, or `-DPercetto_ROOT_DIR=<path>`);
  Monado does not fetch it. Missing → `XRT_HAVE_PERCETTO specified but not available`.
- Links a second Perfetto SDK copy (percetto's); both feed one `traced`. Leave off unless you need
  Monado spans.

## Adding instrumentation

Include `utils/wyrm_trace.h`; no `#ifdef` at call sites.

```cpp
// CPU span (RAII; ends at scope exit)
wyrm::trace::scope s(wyrm::trace::cpu_track::encoder, stream_idx, frame_index, "my_span");

// CPU instant
wyrm::trace::cpu_instant(wyrm::trace::cpu_track::compositor, "my_event", 0, 0);

// CPU begin/end pair (lifetime spans multiple calls)
wyrm::trace::cpu_begin(wyrm::trace::cpu_track::network, stream_idx, frame_idx, "Send");
wyrm::trace::cpu_end  (wyrm::trace::cpu_track::network, stream_idx, frame_idx, "Send");

// GPU span: bracket work with a gpu_timestamp_pool's cmd_begin/cmd_end, emit after the fence
if (auto s = pool.collect(slot))
        wyrm::trace::gpu_slice(wyrm::trace::gpu_track::nvenc_copy,
                                "my_gpu_span", s->begin_ns, s->end_ns, s->frame_index, stream);
```

GPU begin/end come from the pool already in host-monotonic ns (via `VK_EXT_calibrated_timestamps`).
A disabled pool (no timestamp bits, or `WYRM_TRACING` unset) returns `nullopt`. `name` must outlive
the emit (a literal is fine). Place the span at the operation's semantic start, not the function top.

| `cpu_track` | category | track |
| ----------- | -------- | ----- |
| `encoder` | `wyrm_encoder` | Wyrm encoder |
| `compositor` | `wyrm_compositor` | Wyrm compositor |
| `network` | `wyrm_network` | Wyrm network |
