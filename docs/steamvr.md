# Notes for OpenVR and Steam

Wyrm only provides an OpenXR runtime. For games that use OpenVR, [xrizer](https://github.com/Supreeeme/xrizer) or [OpenComposite](https://gitlab.com/znixian/OpenOVR/) should be used to translate the APIs.

If using Steam, games will be sandboxed by pressure vessel, which does not passthrough OpenXR by default.
- Files under `/usr` are mapped into `/run/host/usr`. If xrizer/OpenComposite is installed there, make sure `VR_OVERRIDE` or `~/.config/openvr/openvrpaths.vrpath` uses `/run/host/usr` prefix.
- `PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1` must be set to passthrough the Wyrm OpenXR manifest, library locations, and socket location.
- Steam recognizes the upstream socket name, so Wyrm also needs its socket directory explicitly shared. For example: `PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1 PRESSURE_VESSEL_FILESYSTEMS_RW="$XDG_RUNTIME_DIR/wyrm" %command%`. The server and dashboard suggest a command that includes this directory.

By default, Wyrm attempts to automatically find xrizer and OpenComposite in a number of places:
- When Wyrm is shipped as a flatpak, it uses a provided copy of xrizer by default.
- Wyrm attempts to autodetect xrizer and OpenComposite in a number of places (see `OVR_COMPAT_SEARCH_PATH` in `CMakeLists.txt`).
- The `openvr-compat-path` configuration may be used for an unexpected install location.

If it is found, Wyrm adjusts the `openvrpaths.vrpath` file, and includes whitelisting the xrizer/OpenComposite path and setting `VR_OVERRIDE` in the environment variables.
