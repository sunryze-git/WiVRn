# Debugging Wyrm

This guide covers debugging techniques for Wyrm, including log collection, crash analysis, performance profiling, and common troubleshooting scenarios.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Collecting Logs](#collecting-logs)
  - [HMD Client Logs](#hmd-client-logs)
  - [Server Logs](#server-logs)
- [Starting Components for Debugging](#starting-components-for-debugging)
  - [HMD Client](#hmd-client)
  - [Server](#server)
  - [XR Applications](#xr-applications)
- [Crash Debugging](#crash-debugging)
  - [Server Crashes](#server-crashes)
  - [HMD Client Crashes](#hmd-client-crashes)
- [Performance Profiling](#performance-profiling)
- [Testing two network links](#testing-two-network-links)
- [USB Tunneling](#usb-tunneling)
- [Game Debugging](#game-debugging)
- [Environment Variables Reference](#environment-variables-reference)
- [Configuration Files](#configuration-files)
- [External Resources](#external-resources)
- [Remote Native Debugging (Advanced)](#remote-native-debugging-advanced)

---

## Prerequisites

### ADB Setup

The Android Debug Bridge ([ADB](https://developer.android.com/tools/adb)) is required for debugging the HMD client. Connect your headset to your PC via a USB data cable and authorize the connection when prompted in the HMD.

### Clean Installation

When debugging, ensure the HMD client matches the server version. Perform a clean reinstall:

```bash
# Find and uninstall existing installation
WYRM_PKG=$(adb shell pm list packages | grep wyrm | cut -d: -f2)
adb uninstall ${WYRM_PKG}

# Install the debug APK
adb install /path/to/wyrm-standard-debug.apk
```

---

## Collecting Logs

### HMD Client Logs

The primary method to collect logs from the HMD is through [ADB logcat](https://developer.android.com/tools/logcat).

Watch for crashes (FATAL priority):

```bash
adb logcat '*:F'
```

Record ERROR and above:

```bash
adb logcat '*:E' | tee -a errors-$(date "+%Y-%m-%d-%H").log
```

Wyrm-only logs (verbose):

```bash
adb logcat '*:S' Wyrm:V
```

Watch Wyrm client logs:

```bash
adb logcat | grep Wyrm
```

Watch Android system logs related to Wyrm:

```bash
adb logcat | grep wyrm
```

### Server Logs

Start the server with debug logging enabled:

```bash
XRT_LOG=debug XRT_COMPOSITOR_LOG=debug wyrm-server
```

To also dump video frames for analysis:

```bash
XRT_LOG=debug XRT_COMPOSITOR_LOG=debug WYRM_DUMP_VIDEO=/tmp/video-dump wyrm-server
```

The video dump can be played back with `mpv` or other media players to verify what video was being sent to the HMD.

There will be multiple files in the directory, one for each video stream per codec. For example, `/tmp/video-dump-0.h264`.

---

## Starting Components for Debugging

### HMD Client

The package name varies by build variant:
- `org.wyrmvr.wyrm` - for app store builds
- `org.wyrmvr.wyrm.github` - for GitHub release builds
- `org.wyrmvr.wyrm.local` - for locally built APK
- `org.wyrmvr.wyrm.github.testing` - for CI runs
- `org.wyrmvr.wyrm.github.nightly` - for APK repo

Find your installed package with:

```bash
adb shell pm list packages | grep wyrm
```

Start via ADB:

```bash
WYRM_PKG=$(adb shell pm list packages | grep wyrm | cut -d: -f2)
adb shell am start ${WYRM_PKG}/org.wyrmvr.wyrm.MainActivity
```

Connect to a specific server:

Replace `ADDRESS=localhost` with the address of the server you want to connect to.

```bash
WYRM_PKG=$(adb shell pm list packages | grep wyrm | cut -d: -f2)
ADDRESS=localhost
adb shell am start -a android.intent.action.VIEW -d "wyrm://${ADDRESS}" ${WYRM_PKG}
```

Stop the client:

```bash
WYRM_PKG=$(adb shell pm list packages | grep wyrm | cut -d: -f2)
adb shell am force-stop ${WYRM_PKG}
```

### Server

Start the server with debug logging:

```bash
XRT_LOG=debug XRT_COMPOSITOR_LOG=debug wyrm-server
```

The server will publish itself on the network via mDNS/Zeroconf for discovery by the HMD client.

### XR Applications

Basic OpenXR application:

```bash
xrgears
```

To run Steam games via OpenVR, make sure to add the Steam launch options presented by the server output or in the dashboard. This is required to configure the OpenVR runtime correctly.

Some games have options to control window behavior. Try adding `-windowed` or `-window -w 1280 -h 1024` to the game launch options to allow moving/resizing the local render window. Each game may have different options, check the game's documentation.

> Note: Games linked to OpenVR require [OpenComposite](https://gitlab.com/znixian/OpenOVR) or [xrizer](https://github.com/Supreeeme/xrizer) to translate calls to OpenXR.

---

## Crash Debugging

### Server Crashes

#### Systemd

Core dumps are managed by [systemd-coredump](https://www.freedesktop.org/software/systemd/man/latest/coredumpctl.html).

View the most recent crash:

```bash
coredumpctl info
```

Start a debugger on the most recent crash:

```bash
coredumpctl -1 debug
```

Inside GDB/LLDB:

```
# Select frame 2
f 2

# View local variables
info locals

# Print backtrace
bt
```

#### Non-Systemd Systems

On systems without systemd-coredump, core dumps are handled by the kernel directly.

Check where core dumps are written:

```bash
cat /proc/sys/kernel/core_pattern
```

To set a simple core pattern:

```bash
sudo sysctl -w kernel.core_pattern=/tmp/core.%e.%p.%t
```

This writes cores to `/tmp/` with the format `core.<executable>.<pid>.<timestamp>`.

Analyze a core dump with GDB:

```bash
gdb /usr/bin/wyrm-server /tmp/core.wyrm-server.12345.1706234567
```

Inside GDB:

```
# Print backtrace
bt

# View all threads
info threads

# Switch to thread 2
thread 2

# Print backtrace for all threads
thread apply all bt
```

### HMD Client Crashes

Copy the traceback from the HMD logs using logcat:

```bash
adb logcat '*:F' | grep -A 50 wyrm
```

---

## Performance Profiling

Wyrm supports timing analysis and detailed system-wide profiling with Perfetto.
See [docs/profiling.md](profiling.md) for capturing
timing data, profiling the HMD client and server, and comparing the Vulkan,
NVENC, and VAAPI encoder paths.

---

## Testing two network links

With `WYRM_BUILD_TEST=ON`, build `test-multilink-live` on both Linux hosts.
It uses the transport directly, without an XR runtime or decoder. Start the
server first, then the client, using the same random key from `openssl rand -hex 32`:

```text
test-multilink-live server LOCAL1 IFACE1 LOCAL2 IFACE2 PEER1 PEER_IFACE1 PEER2 PEER_IFACE2 KEY_HEX
test-multilink-live client LOCAL1 IFACE1 LOCAL2 IFACE2 PEER1 PEER_IFACE1 PEER2 PEER_IFACE2 KEY_HEX
```

Use each host's own addresses and interface names for `LOCAL` and `IFACE`, and
the other host's for `PEER` and `PEER_IFACE`. UDP port 9757 must be available on
the test server and reachable over both links. The server learns peer endpoints
from the client. On Steam Frame, use `wlan0` and `wlanap` as the two local interfaces.

The test requires both interfaces to remain active together for one second,
then sends synthetic video payloads at a target of 32 Mbit/s in each direction
for eight seconds. It checks payload integrity and requires at least 100 received
video packets on each interface in at least five shared one-second windows.
Probe traffic does not count. This tests concurrent transport delivery, not
encoded video quality or decoder performance.

For a matched comparison of complete frames, also build `benchmark-multilink-live`
on both hosts. Run `python3 tools/benchmark_frame_links.py --help` for the host,
binary and interface arguments. The runner compares Wi-Fi-only, direct-only and
both-link modes in randomized order, with three trials per mode and bitrate.
Both wireless associations remain connected; the mode selects transport endpoints.

The default workload sends 720 measured frames at 90 Hz after two seconds of
warmup, using fixed rates of 80 and 160 Mbit/s and a 20 ms completion deadline.
Every shard must arrive for a frame to count. The logs retain clock-offset
bounds measured before and after each run, and the results report a range for
on-time frame counts when timing uncertainty crosses a deadline. The analysis
assumes stable monotonic clock rates between those measurements. Sender scheduling
delays count toward completion time. Automatic bitrate adaptation is disabled.

Use `--omit-last-shard` to check the frame counter with an intentionally incomplete
stream. No measured frame should complete. The runner also checks known missing,
late and duplicate completion records. Traffic totals count transport UDP payloads
in both directions, including warmup and control traffic, and exclude lower-layer
headers. The benchmark does not measure encoding, decoding or headset presentation.

## USB Tunneling

For debugging network issues or testing with a wired connection, you can tunnel Wyrm over USB.

Set up the tunnel and connect:

```bash
WYRM_PKG=$(adb shell pm list packages | grep wyrm | cut -d: -f2)
adb reverse tcp:9757 tcp:9757
adb shell am start -a android.intent.action.VIEW -d "wyrm+tcp://localhost:9757" ${WYRM_PKG}
```

You are also able to make this connection through the dashboard.

---

## Game Debugging

### Proton/Wine Games

Enable additional logging for Windows compatibility layers:

```
PROTON_LOG=1 WINEDEBUG=vrclient,openxr,steam %command%
```

### OpenComposite

For games using OpenVR, and you are using [OpenComposite](https://gitlab.com/znixian/OpenOVR) to translate calls to OpenXR. 

The OpenComposite log can be found at `$HOME/.local/state/OpenComposite/logs/opencomposite.log`.

### XRizer

To debug [xrizer](https://github.com/Supreeeme/xrizer), you can set the `RUST_LOG` environment variable to control the log level.

| Variable   | Description                                                                 | Default |
|------------|-----------------------------------------------------------------------------|---------|
| `RUST_LOG` | Configures the logging level for Rust applications. Accepts a comma-separated list of directives in the format `module::path=level`, where `level` can be `error`, `warn`, `info`, `debug`, or `trace`. For example, `RUST_LOG=info,my_crate=debug` sets the global level to `info` and the `my_crate` module to `debug`. | `warn`   |

In xrizer, `RUST_LOG` follows the standard [`env_logger` semantics](https://docs.rs/env_logger/latest/env_logger/#enabling-logging) described above. In addition, xrizer defines the following useful nonstandard logging targets:

| Logging Target | Description |
|-----------------|-------------|
| `openvr_calls` | Logs the name of each OpenVR function as they are called |
| `tracked_property` | Logs the name and device index of each requested tracked device property |

The log is located at `$XDG_STATE_HOME/xrizer/xrizer.txt`, or `$HOME/.local/state/xrizer/xrizer.txt` if `$XDG_STATE_HOME` is not set.

---

## Environment Variables Reference

### Compositor Settings

| Variable | Description | Default |
|----------|-------------|---------|
| `XRT_LOG` | Log level (`trace`, `debug`, `info`, `warn`, `error`) | `warn` |
| `XRT_COMPOSITOR_LOG` | Compositor log level (`trace`, `debug`, `info`, `warn`, `error`) | `info` |
| `OXR_VIEWPORT_SCALE_PERCENTAGE` | Per-application viewport scale (set on application, not server) | `100` |

### Debugging

| Variable | Description |
|----------|-------------|
| `WYRM_DUMP_VIDEO` | Path to dump video frames (e.g., `/tmp/video-dump`); on the headset client it dumps complete frames as handed to the decoder. Each dump has a `.idx` file with `frame offset size` lines |
| `WYRM_MULTILINK_INTERFACES` | Comma separated interface names that multi-link may use (e.g., `wlp9s0`); unset uses every interface |
| `WYRM_LOGLEVEL` | Log level for the native client |
| `WYRM_AUTOCONNECT` | Auto-connect to the first discovered server |
| `WYRM_TRACING` | Enable Wyrm Perfetto tracing: `system` / `inprocess`. Requires `WYRM_USE_PERFETTO=ON` at build time. See [docs/profiling.md](profiling.md). |

### Vulkan

| Variable | Description |
|----------|-------------|
| `VK_INSTANCE_LAYERS` | Vulkan validation layers (e.g., `VK_LAYER_KHRONOS_validation`) |

### OpenXR

| Variable | Description |
|----------|-------------|
| `XR_RUNTIME_JSON` | Path to OpenXR runtime configuration |

See also the [Monado compositor documentation](https://monado.freedesktop.org/getting-started.html#compositor) for additional options.

---

## Configuration Files

### OpenXR Runtime Configuration

Active runtime symlink:

```bash
ls -la $HOME/.config/openxr/1/active_runtime.json
```

Should point to `/usr/share/openxr/1/openxr_wyrm.json` or similar.

Check runtime configuration:

```bash
cat /usr/share/openxr/1/openxr_wyrm.json
```

Example output:

```json
{
    "file_format_version": "1.0.0",
    "runtime": {
        "name": "Monado",
        "library_path": "../../../lib64/wyrm/libopenxr_wyrm.so",
        "MND_libmonado_path": "../../../lib64/wyrm/libmonado_wyrm.so"
    }
}
```

Verify library dependencies:

```bash
pushd /usr/share/openxr/1/
ldd ../../../lib64/wyrm/libopenxr_wyrm.so
popd
```

### Wyrm Server Configuration

See [docs/configuration.md](configuration.md).

---

## External Resources

### Tools and Documentation

- [ADB Documentation](https://developer.android.com/tools/adb) - Android Debug Bridge
- [ADB Logcat](https://developer.android.com/tools/logcat) - Log filtering and analysis
- [coredumpctl](https://www.freedesktop.org/software/systemd/man/latest/coredumpctl.html) - Core dump management
- [Perfetto](https://perfetto.dev/docs/) - System profiling and tracing
- [Perfetto UI](https://ui.perfetto.dev/) - Web-based trace viewer

### Monado/OpenXR

- [Monado Getting Started](https://monado.freedesktop.org/getting-started.html) - Compositor configuration
- [Monado Tracing with Perfetto](https://monado.pages.freedesktop.org/monado/tracing-perfetto.html) - Server-side tracing
- [OpenXR](https://www.khronos.org/openxr/) - Khronos OpenXR standard
- [xrgears](https://github.com/ila-embsys/monado_demo_xrgears) - OpenXR test application

### Gaming

- [OpenComposite](https://gitlab.com/znixian/OpenOVR) - OpenVR to OpenXR translation
- [xrizer](https://github.com/Supreeeme/xrizer) - OpenVR to OpenXR translation layer (Rust)

### Rust

- [env_logger](https://docs.rs/env_logger/latest/env_logger/#enabling-logging) - Rust logging configuration via RUST_LOG

### Android Debugging

- [Android NDK Debugging](https://developer.android.com/ndk/guides/ndk-gdb) - Native debugging with ndk-gdb
- [Android Tracing Quickstart](https://perfetto.dev/docs/quickstart/android-tracing) - Perfetto on Android

---

## Remote Native Debugging (Advanced)

This section covers attaching a native debugger to the HMD client remotely. This requires the Android NDK and familiarity with LLDB/GDB.

Start the client with debugging enabled:

```bash
WYRM_PKG=$(adb shell pm list packages | grep wyrm | cut -d: -f2)
adb shell am start -D ${WYRM_PKG}/org.wyrmvr.wyrm.MainActivity
```

Set up LLDB debugging:

1. Push the LLDB server to the device:

   ```bash
   adb shell "mkdir -p /data/local/tmp/tools"
   adb push "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/lib64/clang/*/lib/linux/aarch64/lldb-server" /data/local/tmp/tools/
   ```

2. Start the LLDB server on the device:

   ```bash
   adb shell /data/local/tmp/tools/lldb-server platform --server --listen '*:5039'
   ```

3. Forward the port:

   ```bash
   adb forward tcp:5039 tcp:5039
   ```

4. Connect from your host:

   ```bash
   lldb
   (lldb) platform select remote-android
   (lldb) platform connect connect://localhost:5039
   ```

Resume the app (required for `-D` flag):

```bash
adb forward tcp:12345 jdwp:$(adb shell pidof ${WYRM_PKG})
jdb -attach localhost:12345
```

See the [Android NDK debugging guide](https://developer.android.com/ndk/guides/ndk-gdb) for more details.
