set(U_GIT_TAG_CPP "${CMAKE_BINARY_DIR}/u_git_tag.cpp")

set(XRT_BUILD_DRIVER_ARDUINO            OFF)
set(XRT_BUILD_DRIVER_BLUBUR_S1          OFF)
set(XRT_BUILD_DRIVER_CONTACTGLOVE       OFF)
set(XRT_BUILD_DRIVER_DAYDREAM           OFF)
set(XRT_BUILD_DRIVER_EUROC              OFF)
set(XRT_BUILD_DRIVER_HDK                OFF)
set(XRT_BUILD_DRIVER_HYDRA              OFF)
set(XRT_BUILD_DRIVER_NS                 OFF)
set(XRT_BUILD_DRIVER_OHMD               OFF)
set(XRT_BUILD_DRIVER_OPENGLOVES         OFF)
set(XRT_BUILD_DRIVER_PSMV               OFF)
set(XRT_BUILD_DRIVER_PSSENSE            OFF)
set(XRT_BUILD_DRIVER_PSVR               OFF)
set(XRT_BUILD_DRIVER_PSVR2              OFF)
set(XRT_BUILD_DRIVER_QWERTY             OFF)
set(XRT_BUILD_DRIVER_REMOTE             OFF)
set(XRT_BUILD_DRIVER_RIFT               OFF)
set(XRT_BUILD_DRIVER_RIFT_S             OFF)
set(XRT_BUILD_DRIVER_ROKID              OFF)
set(XRT_BUILD_DRIVER_SIMULATED          OFF)
set(XRT_BUILD_DRIVER_STEAMVR_LIGHTHOUSE ${WYRM_FEATURE_STEAMVR_LIGHTHOUSE})
set(XRT_BUILD_DRIVER_SOLARXR            ${WYRM_FEATURE_SOLARXR})
set(XRT_BUILD_DRIVER_TWRAP              OFF)
set(XRT_BUILD_DRIVER_UVC                OFF)
set(XRT_BUILD_DRIVER_VF                 OFF)
set(XRT_BUILD_DRIVER_VIVE               OFF)
set(XRT_BUILD_DRIVER_WMR                OFF)
set(XRT_BUILD_DRIVER_XREAL_AIR          OFF)
set(XRT_BUILD_SAMPLES                   OFF)

set(XRT_FEATURE_COMPOSITOR_NULL                   OFF)
set(XRT_FEATURE_DEBUG_GUI                         ${WYRM_FEATURE_DEBUG_GUI})
set(XRT_FEATURE_SERVICE                           ON)
set(XRT_FEATURE_SERVICE_SYSTEMD                   OFF)
set(XRT_FEATURE_SLAM                              OFF)
set(XRT_FEATURE_STEAMVR_PLUGIN                    OFF)
set(XRT_FEATURE_WINDOW_PEEK                       OFF)
set(XRT_FEATURE_OPENXR_ACTIVE_ACTION_SET_PRIORITY ON)
set(XRT_FEATURE_RENDERDOC                         ${WYRM_FEATURE_RENDERDOC})

set(XRT_FEATURE_OPENXR_BODY_TRACKING_FB          ON)
set(XRT_FEATURE_OPENXR_FACE_TRACKING_ANDROID     ON)
set(XRT_FEATURE_OPENXR_FACE_TRACKING2_FB         ON)
set(XRT_FEATURE_OPENXR_FACIAL_TRACKING_HTC       ON)
set(XRT_FEATURE_OPENXR_HAND_TRACKING_EXT         ON)
set(XRT_FEATURE_OPENXR_INTERACTION_EXT_HAND      ON)
set(XRT_FEATURE_OPENXR_INTERACTION_EXT_PALM_POSE ON)
set(XRT_FEATURE_OPENXR_INTERACTION_BYTEDANCE     ON)
set(XRT_FEATURE_OPENXR_INTERACTION_VIVE_FOCUS3   ON)
set(XRT_FEATURE_OPENXR_INTERACTION_TOUCH_PRO     ON)
set(XRT_FEATURE_OPENXR_INTERACTION_TOUCH_PLUS    ON)
set(XRT_FEATURE_OPENXR_LAYER_COLOR_SCALE_BIAS    ON)
set(XRT_FEATURE_OPENXR_LAYER_DEPTH              OFF)
set(XRT_FEATURE_OPENXR_VARJO_QUAD_VIEWS          ON)
set(XRT_FEATURE_OPENXR_SPACE_UNBOUNDED          OFF)

set(XRT_HAVE_BLUETOOTH               OFF)
set(XRT_HAVE_GST                     OFF)

set(XRT_HAVE_JPEG                    OFF)
if (NOT WYRM_FEATURE_DEBUG_GUI)
	set(XRT_HAVE_SDL2                OFF)
endif()
set(XRT_HAVE_OPENCV                  OFF)
set(XRT_HAVE_DBUS                    OFF)
set(XRT_HAVE_LIBUDEV                 OFF)
set(XRT_HAVE_ONNXRUNTIME             OFF)

set(XRT_HAVE_SYSTEMD                 OFF)
set(XRT_INSTALL_SYSTEMD_UNIT_FILES   OFF)

set(BUILD_DOC                        OFF)
set(BUILD_TESTING                    OFF)

set(XRT_HAVE_SYSTEM_CJSON            OFF)

# Drive Monado's tracing from the same switch as Wyrm's: WYRM_TRACE_MONADO builds Monado
# with percetto so its u_trace markers join the WYRM_TRACING session (system backend).
# Monado does not fetch percetto; it must be discoverable (pkg-config / installed, or
# -DPercetto_ROOT_DIR=...). If it is missing, Monado's option_with_deps fails the configure
# with "XRT_HAVE_PERCETTO specified but not available". Linking percetto against Wyrm's own
# Perfetto SDK copy (to drop this requirement and the second runtime) is a planned follow-up.
if(WYRM_TRACE_MONADO)
	message(STATUS "WYRM_TRACE_MONADO=ON: building Monado with percetto tracing (percetto must be available)")
	set(XRT_FEATURE_TRACING ON)
	set(XRT_HAVE_PERCETTO   ON)
endif()

set(XRT_IPC_MSG_SOCK_FILENAME     "wyrm/comp_ipc" CACHE STRING "Service socket filename")
set(XRT_IPC_SERVICE_PID_FILENAME  "wyrm.pid"      CACHE STRING "Service pidfile filename")
set(XRT_OXR_RUNTIME_SUFFIX        "wyrm"          CACHE STRING "OpenXR client library suffix")

FetchContent_MakeAvailable(monado)

if (WYRM_BUILD_SERVER)
	add_executable(wyrm-server
			accept_connection.cpp
			active_runtime.cpp
			avahi_publisher.cpp
			discord_presence.cpp
			hostname.cpp
			main.cpp
			sleep_inhibitor.cpp
			start_application.cpp
			start_systemd_unit.cpp
			ipc_server_cb.cpp
			target_instance_wyrm.cpp
			wyrm_ipc.cpp

			audio/audio_setup.cpp

			compositor/compositor.cpp
			compositor/layer_squasher.cpp
			compositor/foveation.cpp
			compositor/pacer.cpp

			encoder/encoder_settings.cpp
			encoder/idr_handler.cpp
			encoder/video_encoder.cpp
			encoder/video_encoder_raw.cpp

			driver/app_pacer.cpp
			driver/clock_offset.cpp
			driver/configuration.cpp
			driver/hand_joints_list.cpp
			driver/pose_list.cpp
			driver/tracking_control.cpp
			driver/view_list.cpp
			driver/wyrm_hmd.cpp
			driver/wyrm_controller.cpp
			driver/wyrm_eye_tracker.cpp
			driver/wyrm_gamepad.cpp
			driver/wyrm_android_face_tracker.cpp
			driver/wyrm_body_tracker.cpp
			driver/wyrm_fb_face2_tracker.cpp
			driver/wyrm_htc_face_tracker.cpp
			driver/wyrm_generic_tracker.cpp
			driver/wyrm_uinput.cpp
			driver/wyrm_session.cpp
			driver/wyrm_connection.cpp
			driver/xrt_cast.cpp

			utils/wyrm_vk_bundle.cpp
			utils/wyrm_trace.cpp
			utils/gpu_timestamp_pool.cpp
		)
	target_compile_features(wyrm-server PRIVATE cxx_std_20)
	target_compile_definitions(wyrm-server PRIVATE VULKAN_HPP_NO_CONSTRUCTORS)

	target_include_directories(wyrm-server SYSTEM PRIVATE ${monado_SOURCE_DIR}/src/xrt/compositor/)
	target_include_directories(wyrm-server PRIVATE .)

	target_link_libraries(wyrm-server PRIVATE CLI11::CLI11 xrt-external-renderdoc)

	if (WYRM_FEATURE_STEAMVR_LIGHTHOUSE)
		target_include_directories(wyrm-server SYSTEM PRIVATE ${monado_SOURCE_DIR}/src/xrt/drivers/steamvr_lh/)
		target_link_libraries(wyrm-server PRIVATE drv_steamvr_lh)
	endif()

	if (WYRM_FEATURE_SOLARXR)
		target_include_directories(wyrm-server SYSTEM PRIVATE ${monado_SOURCE_DIR}/src/xrt/drivers/solarxr/)
		target_link_libraries(wyrm-server PRIVATE drv_solarxr)
	endif()

	if(WYRM_USE_NVENC)
		target_sources(
			wyrm-server
			PRIVATE encoder/video_encoder_nvenc.cpp
				encoder/video_encoder_nvenc_shared_state.cpp
			)
	endif()

	if(WYRM_USE_VAAPI)
		target_sources(
			wyrm-server
			PRIVATE encoder/ffmpeg/video_encoder_ffmpeg.cpp
				encoder/ffmpeg/video_encoder_va.cpp
				encoder/ffmpeg/ffmpeg_helper.cpp
			)
		target_link_libraries(wyrm-server PRIVATE PkgConfig::LIBAV PkgConfig::LIBDRM)
	endif()

	if(WYRM_USE_VULKAN_ENCODE)
		target_sources(wyrm-server PRIVATE
			encoder/video_encoder_vulkan.cpp
			encoder/video_encoder_vulkan_h264.cpp
			encoder/video_encoder_vulkan_h265.cpp
			)
	endif()

	if(WYRM_USE_X264)
		target_sources(wyrm-server PRIVATE encoder/video_encoder_x264.cpp)
		target_link_libraries(wyrm-server PRIVATE PkgConfig::X264)
	endif()

	if(WYRM_USE_PERFETTO)
		find_path(PERFETTO_SDK_DIR
			NAMES perfetto.h
			PATHS /usr/share/perfetto/sdk
			DOC "Perfetto amalgamated SDK directory")
		if(NOT PERFETTO_SDK_DIR)
			message(FATAL_ERROR "WYRM_USE_PERFETTO=ON but Perfetto SDK (perfetto.h) not found. Install perfetto-sdk.")
		endif()
		add_library(wyrm-perfetto-sdk STATIC ${PERFETTO_SDK_DIR}/perfetto.cc)
		target_include_directories(wyrm-perfetto-sdk PUBLIC ${PERFETTO_SDK_DIR})
		target_compile_options(wyrm-perfetto-sdk PRIVATE -w)
		target_link_libraries(wyrm-perfetto-sdk PUBLIC pthread dl)
		target_link_libraries(wyrm-server PRIVATE wyrm-perfetto-sdk)
		target_compile_definitions(wyrm-server PRIVATE WYRM_USE_PERFETTO)
		if(WYRM_TRACE_MONADO)
			target_compile_definitions(wyrm-server PRIVATE WYRM_TRACE_MONADO)
		endif()
	endif()

	if(WYRM_USE_PIPEWIRE)
		target_sources(
			wyrm-server
			PRIVATE audio/audio_pipewire.cpp
			)
		target_link_libraries(wyrm-server PRIVATE PkgConfig::libpipewire)
	endif()

	# Generate dbus interface code as a separate target to avoid warnings in generated code
	add_custom_command(OUTPUT wyrm_server_dbus.c wyrm_server_dbus.h
			   COMMAND ${GDBUS_CODEGEN} ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/org.wyrmvr.Server.xml --interface-prefix org.wyrmvr --generate-c-code wyrm_server_dbus --c-namespace Wyrm
			   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/org.wyrmvr.Server.xml)
	add_custom_target(wyrm-server-dbus-codegen ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/wyrm_server_dbus.c ${CMAKE_CURRENT_BINARY_DIR}/wyrm_server_dbus.h)
	add_library(wyrm-server-dbus STATIC
		${CMAKE_CURRENT_BINARY_DIR}/wyrm_server_dbus.c
		${CMAKE_CURRENT_BINARY_DIR}/wyrm_server_dbus.h
		)
	target_include_directories(wyrm-server-dbus PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
	add_dependencies(wyrm-server-dbus wyrm-server-dbus-codegen)
	target_link_libraries(wyrm-server-dbus PUBLIC PkgConfig::glib2)
	target_link_libraries(wyrm-server PUBLIC wyrm-server-dbus)
	target_link_libraries(wyrm-server PUBLIC PkgConfig::libnotify)

	add_custom_command(OUTPUT systemd_manager.c systemd_manager.h
			   COMMAND ${GDBUS_CODEGEN} ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/org.freedesktop.systemd1.Manager.xml --interface-prefix org.freedesktop.systemd1 --generate-c-code systemd_manager --c-namespace systemd
			   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/org.freedesktop.systemd1.Manager.xml )
	add_custom_command(OUTPUT systemd_unit.c systemd_unit.h
		COMMAND ${GDBUS_CODEGEN} ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/org.freedesktop.systemd1.Unit.xml --interface-prefix org.freedesktop.systemd1 --generate-c-code systemd_unit --c-namespace unit
			   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/org.freedesktop.systemd1.Manager.xml )
	target_sources(wyrm-server-dbus PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/systemd_manager.c ${CMAKE_CURRENT_BINARY_DIR}/systemd_manager.h
		${CMAKE_CURRENT_BINARY_DIR}/systemd_unit.c ${CMAKE_CURRENT_BINARY_DIR}/systemd_unit.h)

	target_link_libraries(
		wyrm-server
		PRIVATE
			aux_os
			aux_render
			aux_util
			aux_vk
			base
			comp_multi
			comp_util
			ipc_server
			target_helpers
			xrt-external-openxr
			xrt-interfaces

			PkgConfig::AVAHI
			Eigen3::Eigen
			wyrm-common
			wyrm-common-server
			wyrm-external
			nlohmann_json::nlohmann_json
		)

	if(WYRM_BUILD_TEST)
		add_executable(test-discord-presence test_discord_presence.cpp discord_presence.cpp)
		target_link_libraries(test-discord-presence PRIVATE nlohmann_json::nlohmann_json)
		add_test(NAME discord-presence COMMAND test-discord-presence)
		add_executable(test-pose-prediction driver/test_pose_prediction.cpp driver/pose_list.cpp driver/clock_offset.cpp)
		target_include_directories(test-pose-prediction PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
		target_link_libraries(test-pose-prediction PRIVATE wyrm-common aux_util Eigen3::Eigen)
		add_test(NAME pose-prediction COMMAND test-pose-prediction)
		add_executable(test-pacer compositor/test_pacer.cpp compositor/pacer.cpp encoder/idr_handler.cpp driver/clock_offset.cpp)
		target_include_directories(test-pacer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${monado_SOURCE_DIR}/src/xrt/compositor)
		target_link_libraries(test-pacer PRIVATE wyrm-common aux_util aux_vk comp_util)
		add_test(NAME pacer COMMAND test-pacer)
		if(WYRM_USE_NVENC)
			add_executable(test-nvenc-rate-control encoder/test_nvenc_rate_control.cpp)
			target_link_libraries(test-nvenc-rate-control PRIVATE wyrm-common)
			add_test(NAME nvenc-rate-control COMMAND test-nvenc-rate-control)
		endif()
	endif()

	include(CompileGLSL)
	FetchContent_GetProperties(monado SOURCE_DIR _monado_src)
	wyrm_compile_glsl(wyrm-server
		TARGET_ENV vulkan1.3
		SHADERS
			"${_monado_src}/src/xrt/auxiliary/render/shaders/layer.comp"
			"${CMAKE_CURRENT_SOURCE_DIR}/compositor/shaders/foveation.comp"
		)

	target_compile_definitions(wyrm-server PRIVATE JSON_DIAGNOSTICS=1)

	configure_file(dist/wyrm.service.in wyrm.service)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wyrm.service
		DESTINATION lib/systemd/user/)

	install(FILES dist/firewalld-wyrm.xml
		RENAME wyrm.xml
		DESTINATION lib/firewalld/services)

	install(TARGETS wyrm-server)
endif()

if (WYRM_BUILD_SERVER_LIBRARY)
	set_target_properties(${RUNTIME_TARGET} PROPERTIES EXCLUDE_FROM_ALL FALSE)
	set_target_properties(monado PROPERTIES EXCLUDE_FROM_ALL FALSE)

	set(_extra_args)
	if (WYRM_OPENXR_MANIFEST_TYPE STREQUAL "absolute")
		set(_extra_args ABSOLUTE_RUNTIME_PATH)
	elseif (WYRM_OPENXR_MANIFEST_TYPE STREQUAL "relative")
		set(_extra_args RUNTIME_DIR_RELATIVE_TO_MANIFEST ../../../${CMAKE_INSTALL_LIBDIR}/wyrm)
	endif()

	set_target_properties(monado PROPERTIES OUTPUT_NAME "monado_wyrm")
	# Workaround as generate_openxr_runtime_manifest_at_install uses target name as library path
	add_library(monado_wyrm ALIAS monado)

	if (WYRM_OPENXR_MANIFEST_ABI)
		if (CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "i.86")
			set(_filename openxr_wyrm.i686.json)
		else()
			set(_filename openxr_wyrm.${CMAKE_C_COMPILER_ARCHITECTURE_ID}.json)
		endif()
	else()
		set(_filename openxr_wyrm.json)
	endif()

	generate_openxr_runtime_manifest_at_install(
		${_extra_args}
		RUNTIME_TARGET ${RUNTIME_TARGET}
		DESTINATION share/openxr/1
		RELATIVE_RUNTIME_DIR ${CMAKE_INSTALL_LIBDIR}/wyrm
		LIBMONADO monado_wyrm
		OUT_FILENAME ${_filename}
		MANIFEST_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/dist/openxr_manifest.in.json"
		)

	install(TARGETS ${RUNTIME_TARGET} monado
		LIBRARY
		DESTINATION ${CMAKE_INSTALL_LIBDIR}/wyrm
		COMPONENT Runtime)
endif()
