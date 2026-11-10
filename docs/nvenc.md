# Notes for NVIDIA NVENC encoder

### Introduction
NVIDIA GPUs use performance states (P-states), which influence how the driver manages GPU core and memory clocks. P0 is generally the highest-performance state, while P8 is typically one of the lowest-power states. 

WiVRn's NVIDIA NVENC encoder implementation relies on CUDA, which may cause NVIDIA drivers to keep the GPU in the P2 performance state instead of allowing it to enter P0.
This behavior is associated with CUDA workloads and may be used by NVIDIA drivers to improve stability.

NVIDIA drivers support an application profile setting named `CudaNoStablePerfLimit`, users have reported that it resolves the issue. [open-gpu-kernel-modules](https://github.com/NVIDIA/open-gpu-kernel-modules/issues/333#issuecomment-3669477571)

Some distributions already ship application profiles that exclude specific applications from these performance limits. You can see examples of how they apply this in [Pop!_OS](https://github.com/pop-os/nvidia-graphics-drivers/blob/master/debian/application-profiles/cuda-no-stable-perf-limit) or [CachyOS](https://github.com/CachyOS/CachyOS-PKGBUILDS/blob/master/nvidia/nvidia-utils/cuda-no-stable-perf-limit).

### Identifying the issue

To check whether your system is affected, monitor the GPU P-State using `watch nvidia-smi -q -d PERFORMANCE` while playing a game with the NVIDIA NVENC encoder enabled. If the GPU remains in P2 under load while using NVENC, but reaches P0 with the Vulkan encoder or after applying this profile, the system is likely affected.

### Workaround instructions
> [!NOTE] 
> If your GPU already enters P0 while using the NVIDIA encoder, this workaround is unlikely to provide any benefit.

1. Create the NVIDIA configuration directory

    The application profiles configuration file is located at:

    `$HOME/.nv/nvidia-application-profiles-rc`

    If the `.nv` directory does not already exist, create it:

    `mkdir -p "$HOME/.nv"`

2. Create the configuration file

    Open `$HOME/.nv/nvidia-application-profiles-rc` in your preferred text editor and add the following contents:
```
{
    "profiles": [
        {
            "name": "WiVRnServer",
            "settings": [
                {
                  "key"   : "0x166c5e",
                  "value" : 0
                }
            ]
        }
    ],
    "rules": [
        { "pattern" : { "feature" : "procname", "matches" : "wivrn-server" }, "profile" : "WiVRnServer" }
    ]
}
```
3. Save the file

    Save the file and `restart wivrn-server`. The profile should be applied automatically by the NVIDIA driver.

| Settings key | Reference name | Value | Description |
| --- | --- | :---: | --- |
| `0x166c5e` | CUDA - Force P2 State | 0 | Controls forcing of the CUDA P-State. When set to 0, prevents automatic forcing of a specific P-State. |
<!--
| `GLVRRAllowed` | G-SYNC VRR | false | Enables VRR support for applications on G-SYNC compatible monitors. Set to false to disable this feature. |
| `VKDirectGSYNCAllowed` | Vulkan Direct G-SYNC | false | Enables VRR support for vulkan direct-to-display applications on G-SYNC compatible monitors. Set to false to disable this feature. |
| `VKDirectGSYNCCompatibleAllowed` | Vulkan Direct Compatible G-SYNC | 0 | Allows VRR on validated or unvalidated G-SYNC compatible monitors for Vulkan Direct-to-Display applications. Set to value 0 to disable this feature. |
| `GLShowGraphicsOSD` | Graphics API OSD | false | Control whether the graphics API visual indicator is rendered on top of OpenGL and Vulkan applications. Set to false to disable this feature. |
-->

## Quality options

The NVENC backend accepts string options in the server configuration:

```json
{
  "encoder": {
    "encoder": "nvenc",
    "codec": "h265",
    "options": {
      "preset": "P6",
      "tuning": "ultra-low-latency",
      "multipass": "full",
      "aq": "true"
    }
  }
}
```

This selects more encoding work than the defaults. Check that encoding still
fits your frame budget. No quality gain or encode time is guaranteed across GPUs.
The default settings remain P4, ultra-low-latency, quarter-resolution multipass
and spatial AQ. Options persist across live bitrate and frame-rate updates.

| Option | Values | Default |
| --- | --- | --- |
| `preset` | `P1` through `P7` | `P4` |
| `tuning` | `ultra-low-latency`, `low-latency`, `high-quality` | `ultra-low-latency` |
| `multipass` | `disabled`, `quarter`, `full` | `quarter` |
| `rc` | `cbr`, `vbr`, `cqp` | `cbr` |
| `aq` | `true`, `false` | `true` |
| `temporal-aq` | `true`, `false`; GPU support checked | `false` |
| `aq-strength` | `0` (automatic) through `15` | `0` |
| `vbv-frames` | `1` through `16` | `2` |
| `vbv-initial-frames` | `0` through buffer size | `1` |
| `qp` | `0` through `51`, for constant QP | `23` |
| `min-qp`, `max-qp` | `0` through `51`, minimum must not exceed maximum | unset |
| `max-bitrate` | Peak bits per second; effective value is at least target bitrate | target bitrate |
| `gop` | Positive frame count; `4294967295` means infinite | infinite |
| `intra-refresh-period` | `0` (disabled) through `10000` frames | `100` |
| `intra-refresh-count` | `1` through period, when enabled | `50` |

Unknown options and invalid ranges are rejected. Lookahead and B-frames remain
disabled: buffering them is incompatible with the current synchronous streaming
pipeline. Explicit nonzero values are rejected. The preset is queried before
WiVRn applies its streaming constraints. Intra refresh remains capability checked.
See NVIDIA's [NVENC programming guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html)
for the underlying quality and rate-control behavior.

Editing these server options does not replace the encoder immediately. Reconnect
the headset or apply a video setting on the headset to renegotiate them. Existing
client and server processes can remain running; both must already be builds that
support live stream handoff.
