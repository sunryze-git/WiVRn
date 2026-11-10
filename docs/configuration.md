# Configurable items

Configuration is split between headset and server. Headset contains most configuration, while server has system specific items such as encoders and paths.
Files are read from
- `/usr/share/wivrn/config.json` (where `/usr` is selected at configure time with `CMAKE_INSTALL_PREFIX`)
- `/etc/wivrn/config.json`
- `$XDG_CONFIG_HOME/wivrn/config.json` or if `$XDG_CONFIG_HOME` is not set, `$HOME/.config/wivrn/config.json`.

Files later in the list replace top-level values from previous ones.

If you installed WiVRn from a flatpack, the config is in `$HOME/.var/app/io.github.wivrn.wivrn/config/wivrn/config.json`.

All elements are optional and have default values.

## Changing video settings while streaming

The headset's in-stream Video and Streaming pages allow changing resolution,
foveated encoding, codec, and color bit depth without restarting WiVRn or closing
the running game. Slider changes are applied after a short pause in adjustment.
Video can pause briefly while the encoder and decoder resources are replaced;
audio and tracking remain connected. Bitrate and refresh rate also update live.
VAAPI rate changes rebuild the encoder context because changing its FFmpeg fields
does not reliably update the hardware rate control. Rapid changes are coalesced
while a previous handoff is still awaiting decoded frames.

The server retains the previous stream until both eyes decode its replacement.
Unsupported settings or failed resource allocations leave the previous stream
available and display an error. Server configuration still takes precedence over
headset codec and bit-depth preferences.

Resolution changes update the compositor target and the recommendation for new
applications. Running games can keep their existing render targets; increasing
the stream resolution cannot add detail that the game has not rendered. Adjust
the game's own resolution setting when available.

Video settings are also renegotiated when reconnecting to a running server.
Changes to the audio or tracking devices exposed to OpenXR applications can still
require recreating the OpenXR system. Both client and server must use this version
of the streaming protocol; existing running installations are not upgraded in place.

On the tested Steam Frame driver, the hardware decoder advertises NV12 output
and does not advertise P010. WiVRn therefore negotiates 8-bit video even when
10-bit is selected. It keeps the capability check rather than forcing an
unadvertised decoder format.

SolarXR uses the SolarXR driver protocol for SlimeVR, including reconnect and
battery-status support. The `SLIMEVR_SOCKET_DIR` environment variable can override
the default IPC socket directory.

## Steam Frame refresh changes

On Linux headsets reporting native panel modes such as `2*2160x2160_120`, WiVRn
collects all advertised modes and freezes that capability list for the client
session. The current physical rate is reported separately. Frame's current-rate
query is polled once per second as a fallback when its runtime omits a refresh
change event. SteamOS remains responsible for externally managed physical modes.
Changing a mode no longer makes a reconnect incompatible. The server keeps its
OpenXR enumeration stable, resets pacing history on rate changes, and applies the
half-rate switch against the observed physical rate.

These changes address WiVRn's stale mode and pacing paths. They do not establish
that all jitter reported in [#1173](https://github.com/WiVRn/WiVRn/issues/1173)
is fixed; that still requires measurement on a Frame and its runtime.

## `video-pacing`

Default value: `1.25`. Set to `0` to disable; otherwise use a value from `1` to `4`.
This multiplies the configured aggregate video bitrate to set the packet send
budget, with an 8 KiB burst allowance. Both eyes, alpha, UDP frames and TCP
keyframes share the budget. Video is fragmented into small shards on both
transports. Audio and other control traffic do not consume this budget.

This smooths bursts; it does not measure available Wi-Fi capacity or adapt the
encoder bitrate to congestion. Leave room for keyframes, protocol overhead and
other traffic. Constant-QP NVENC output is variable and may exceed this budget.
Lost keyframes or acknowledgments are retried after 200 ms, including with the
Vulkan encoder. Terminal decoder failures trigger at most three fresh decoder
attempts per 30 seconds, then display an error; changing codec or resolution can
initiate another handoff.

## `quad-views`

Default value: `true`. Exposes `XR_VARJO_quad_views` and the corresponding OpenXR
four-view configuration to applications that support it. The two peripheral views
recommend half the stereo width and height; the two inset views recommend
three quarters. Insets cover half the tangent-space span of their corresponding
peripheral view, so their recommended pixel density is higher.

WiVRn supplies identical poses for each eye's peripheral and inset view and keeps
the inset entirely within the peripheral FOV. Insets follow the headset's per-eye
foveation centers, including eye tracking when enabled. Missing or stale centers
fall back to centered insets after 500 ms. Monado splits the application layer and
its existing shader blends each inset into its peripheral image before WiVRn's
normal foveation and two-eye encoding. No extra decoder streams are required.

Applications must select the four-view configuration and handle mutable FOVs.
This does not add quad views to games that only render stereo. It does not expose
`XR_VARJO_foveated_rendering`; applications requiring that separate extension
need additional support. Disable `quad-views` for application compatibility.

## `bit-depth`
Default value: 10-bit if supported by the selected codec, encoder and decoder. 8-bit otherwise.

Bit depth of the video. 8-bit is supported by all encoders. 10-bit is supported by `vulkan`, `vaapi` and `nvenc` with `h265` or `av1` codecs.

## `encoder`
The encoder to use, either a single string or object applied to all streams, or a list of string or objects with values for left, right and alpha.
When a string it is used, it is equivalent to the `encoder` item of the object.

WiVRn encodes each eye separately, and the alpha channel as one for both eyes. Each stream is processed independently, this may use resources more effectively and reduce latency.

### `encoder`
Default value, in order: `vulkan` if supported, `nvenc` on Nvidia GPU, `vaapi` on all other GPU, else `x264`.

Identifier of the encoder, one of
* `x264`: software encoding
* `nvenc`: Nvidia hardware encoding
* `vaapi`: AMD/Intel hardware encoding
* `vulkan`: Vulkan Video encode, cross-vendor hardware encoding

### `codec`
Default value: best supported by both headset and encoder of `av1`, `h264`, `h265`.

One of `h264`, `h265`, `av1`, `raw`.

Not all encoders support every codec:
- `x264` encoder only supports `h264` codec
- `vulkan` encoder supports `h264` and `h265` codecs
- `raw` encoder only supports `raw` codec
- `nvenc` and `vaapi` support all codecs, except `raw`

If `nvenc` encoder is in use, you can refer to [nvidia website](https://developer.nvidia.com/video-encode-decode-support-matrix) to make sure that your GPU supports encoding with the desired codec.

### Examples
1. Simple configuration
```json
{
	"encoder": {
		"encoder": "vaapi",
		"codec": "h265"
	}
}
```
Use vaapi hardware encoding, h265 video codec (HEVC).

2. Hardware + software encoder
```json
{
	"encoder": [
		{
			"encoder": "vaapi",
			"codec": "h265",
		},
		{
			"encoder": "x264",
			"codec": "h264",
		},
		{
			"encoder": "vaapi",
			"codec": "h265",
		},
	]
}
```
Creates a hardware encoder for left eye and transparency, and a software encoder for right eye.

### `device`, only for vaapi
Default value: unset

Manually specify the device for encoding, can be used to offload encode to an iGPU. Device shall be in the form "/dev/dri/renderD128".


### `options` (very advanced), for vaapi and nvenc
Default value: unset

For VAAPI, a JSON object of additional options passed directly to ffmpeg `avcodec_open2`'s `option` parameter. For NVENC, see the [validated quality options](nvenc.md#quality-options). Options are strings. Server encoder configuration is read when video settings are renegotiated, including on reconnect; changed options trigger the same encoder handoff as codec changes.

## `application`
Default value: unset

An application to start when connection with the headset is established, can be a string or an array of strings if parameters need to be provided.

### Example
```json
{
	"application": ["steam", "steam://launch/275850/VR"]
}
```
Launch No Man's Sky in VR mode on Steam when connection with headset is established.

## `tcp-only`
Default value: `false`

Only use TCP for communications with the client, this may have increased latency.
If `false` or unset, WiVRn will use both TCP and UDP.

### Example
```json
{
	"tcp-only": true
}
```

## `publish-service`
Default value: `avahi`

How to publish the service over the network, `avahi` or null.

If set to null, service will not be published and address has to be entered manually on the headset.

## `openvr-compat-path`
Default value: unset

Provides the path to the directory of an OpenVR compatibility tool (such as OpenComposite).

If unset, WiVRn will autodetect the path of such a tool as usual (see [the SteamVR guide](./steamvr.md)).

If set to an null, WiVRn will not manage the OpenVR configuration.

## `hid-forwarding`
Default value: `false`

Only available when the `uinput` kernel module is loaded and the user has write access.

Mirrors input devices forwarded from the headset (keyboard, mouse, gamepad) to `uinput`
devices, for applications that do not read them through OpenXR. Which devices are forwarded is
chosen on the headset.

A forwarded gamepad is also exposed as a native OpenXR gamepad at `/user/gamepad`, which needs
no permission and is always available. Only the digested controller state is forwarded
(buttons, two sticks, analog triggers and a d-pad), so device specific features such as gyro,
adaptive triggers or the touchpad are not available.

## `debug-gui`
Default value: `false`

Only available when built with `WIVRN_FEATURE_DEBUG_GUI`.

Enables the Monado debug gui.

## `use-steamvr-lh`
Default value: `false`

Only available when built with `WIVRN_FEATURE_STEAMVR_LIGHTHOUSE`.

Enables the driver to load SteamVR Lighthouse devices.

## `lh-max-extrapolation`
Default value: unset

Only available when built with `WIVRN_FEATURE_STEAMVR_LIGHTHOUSE`.

Maximum time in milliseconds that poses may be extrapolated ahead for SteamVR Lighthouse devices.

## `lh-stick-deadzone`
Default value: `0`

Only available when built with `WIVRN_FEATURE_STEAMVR_LIGHTHOUSE`

Applies a deadzone to joysticks on SteamVR controllers (e.g. Index).

## `port`
Default value: `9757`

Change the TCP/UDP port used for the connection.

## `hostname`
Default value: unset

If set, overrides the name displayed in the server list.
