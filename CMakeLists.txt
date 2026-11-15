cmake_minimum_required(VERSION 3.28)
project(Wyrm VERSION 0.0.0 LANGUAGES C CXX)

# Set default for FetchContent projects
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if(ANDROID)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/android)
endif()

# Default to PIC code
include(CheckPIESupported)
check_pie_supported()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
include(ExternalProject)
include(GNUInstallDirs)

set(GIT_TAG "" CACHE STRING "Build for a tag (from git if empty)")
set(GIT_DESC "" CACHE STRING "Version to show in UI (from git if empty, exclusive with GIT_TAG)")
set(GIT_COMMIT "" CACHE STRING "Git SHA used to fetch client APK (from git if empty, exclusive with GIT_TAG)")

if (GIT_TAG AND (GIT_DESC OR GIT_COMMIT))
	message(FATAL_ERROR "cannot use GIT_DESC or GIT_COMMIT when using GIT_TAG")
endif()

option(WYRM_USE_SYSTEM_FREETYPE "Use system FreeType" ON)
option(WYRM_USE_SYSTEM_BOOST "Use system Boost headers" ON)
option(WYRM_USE_SYSTEM_OPENXR "Use system OpenXR headers" ON)
option(WYRM_USE_SYSTEM_LIBKTX "Use system KTX-Software library" ON)
option(WYRM_COMPRESS_GLB "Compress textures in GLB files with gltf-transform" OFF)
option(WYRM_USE_ANDROID_VALIDATION_LAYER "Download the android version of the Vulkan validation layer" OFF)
option(WYRM_USE_ANDROID_BIN_LIBKTX "Download the prebuilt libktx for android" OFF)
option(WYRM_BUILD_CLIENT "Build Wyrm client" OFF)
option(WYRM_BUILD_SERVER "Build Wyrm server" ON)
option(WYRM_BUILD_SERVER_LIBRARY "Build OpenXR library for Wyrm server" ${WYRM_BUILD_SERVER})
option(WYRM_BUILD_DASHBOARD "Build Wyrm dashboard" OFF)
option(WYRM_BUILD_WYRMCTL "Build wyrmctl" ON)
option(WYRM_BUILD_DISSECTOR "Build Wireshark dissector" OFF)
option(WYRM_BUILD_TEST "Build test binaries" OFF)
set(FRAME_ABI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../wayvr/frame-abi/include" CACHE PATH "Directory of the frame shell's frame_abi.h")
if(WYRM_BUILD_TEST)
    enable_testing()
endif()
option(WYRM_WERROR "Treat warnings as errors" OFF)
option(WYRM_OPTIMIZE_SHADERS "Run spirv-opt on compiled shaders" ON)
option(WYRM_DEBUG_SHADERS "Compile shaders with debug information" OFF)
option(WYRM_ADDRESS_SANITIZER "Use address sanitizer" OFF)

# V4L2 decoder on by default on Linux AArch64
set(_WYRM_USE_V4L2_DEFAULT OFF)
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" AND NOT ANDROID)
    set(_WYRM_USE_V4L2_DEFAULT ON)
endif()
option(WYRM_USE_V4L2 "Enable V4L2 hardware video decoder" ${_WYRM_USE_V4L2_DEFAULT})
option(WYRM_FRAME_EYES "Steam Frame eye expressions (needs ExecuTorch, see WYRM_EXECUTORCH_PREFIX)" OFF)
set(WYRM_EXECUTORCH_PREFIX "" CACHE PATH "Install prefix of the ExecuTorch runtime for WYRM_FRAME_EYES")
unset(_WYRM_USE_V4L2_DEFAULT)

if(WYRM_OPTIMIZE_SHADERS AND WYRM_DEBUG_SHADERS)
    message(FATAL_ERROR "WYRM_OPTIMIZE_SHADERS and WYRM_DEBUG_SHADERS are incompatible")
endif()

option(WYRM_USE_NVENC "Enable nvenc (Nvidia) hardware encoder" ON)
option(WYRM_USE_VAAPI "Enable vaapi (AMD/Intel) hardware encoder" ON)
option(WYRM_USE_VULKAN_ENCODE "Enable vulkan video encoder" ON)
option(WYRM_USE_X264 "Enable x264 software encoder" ON)

option(WYRM_USE_PERFETTO "Enable Perfetto tracing via the Perfetto SDK (server only)" OFF)
option(WYRM_TRACE_MONADO "Also instrument Monado (percetto) under the same WYRM_TRACING switch; pulls in a second Perfetto runtime, system backend only. Requires WYRM_USE_PERFETTO" OFF)
if(WYRM_TRACE_MONADO AND NOT WYRM_USE_PERFETTO)
	message(FATAL_ERROR "WYRM_TRACE_MONADO=ON requires WYRM_USE_PERFETTO=ON")
endif()

option(WYRM_USE_PIPEWIRE "Enable pipewire backend" ON)

set(OVR_COMPAT_SEARCH_PATH "/opt/xrizer:/usr/local/lib/OpenComposite:/usr/lib/OpenComposite:/opt/OpenComposite:/opt/opencomposite:/opt/VapoR:/usr/local/lib/VapoR"
    CACHE STRING "List of places to search for the OpenVR compatibility layer, separated by :")

option(WYRM_FEATURE_DEBUG_GUI "Enable Monado debug GUI" OFF)
option(WYRM_FEATURE_RENDERDOC "Support renderdoc" OFF)
option(WYRM_FEATURE_SOLARXR "Enable SolarXR driver" ON)
option(WYRM_FEATURE_STEAMVR_LIGHTHOUSE "Enable SteamVR Lighthouse driver" ON)

set(WYRM_CSS "${CMAKE_SOURCE_DIR}/images/wyrm-local.css" CACHE STRING "Stylesheet to use to render SVGs" )

option(WYRM_SHOW_IMGUI_DEMO_WINDOW "Show the Dear ImGui demo window in the client" OFF)
option(WYRM_CLIENT_DEBUG_MENU "Enable the debug menu in the client" OFF)

set(WYRM_OPENXR_MANIFEST_TYPE_VALUES relative absolute filename)
set(WYRM_OPENXR_MANIFEST_TYPE "relative" CACHE STRING "How to reference libraries in OpenXR manifest: ${WYRM_OPENXR_MANIFEST_TYPE_VALUES}")
if (NOT WYRM_OPENXR_MANIFEST_TYPE IN_LIST WYRM_OPENXR_MANIFEST_TYPE_VALUES)
    message(FATAL_ERROR "WYRM_OPENXR_MANIFEST_TYPE must be one of ${WYRM_OPENXR_MANIFEST_TYPE_VALUES}")
endif()
option(WYRM_OPENXR_MANIFEST_ABI "Generate an ABI specific OpenXR manifest")

option(ENABLE_COLOURED_OUTPUT "Always produce ANSI-coloured output (GNU/Clang only)." ON)

# Share the upstream environment catalog; this is an asset dependency, not a release source.
set(WYRM_ENVIRONMENTS_URL "https://github.com/Wyrm/Wyrm-environments/releases/latest/download/index.json" CACHE STRING "URL hosting the custom environments json file")
set(_WYRM_DEFAULT_REPOSITORY "sunryze-git/Wyrm")
if(DEFINED ENV{WYRM_GITHUB_REPOSITORY})
    set(_WYRM_DEFAULT_REPOSITORY "$ENV{WYRM_GITHUB_REPOSITORY}")
endif()
set(WYRM_GITHUB_REPOSITORY "${_WYRM_DEFAULT_REPOSITORY}" CACHE STRING "GitHub owner/repository for Wyrm releases and project links (empty disables online release discovery)")
if(WYRM_GITHUB_REPOSITORY AND NOT WYRM_GITHUB_REPOSITORY MATCHES "^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
    message(FATAL_ERROR "WYRM_GITHUB_REPOSITORY must be a GitHub owner/repository")
endif()

if (WYRM_BUILD_CLIENT OR WYRM_BUILD_SERVER OR WYRM_BUILD_DASHBOARD OR WYRM_BUILD_DISSECTOR)
    set(NEED_COMMON TRUE)
else()
    set(NEED_COMMON FALSE)
endif()

if(ENABLE_COLOURED_OUTPUT)
	if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
		add_compile_options(-fdiagnostics-color=always)
	elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		add_compile_options(-fcolor-diagnostics)
	endif()
endif()

find_package(PkgConfig REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Git)
if (NEED_COMMON)
    find_package(OpenSSL REQUIRED)
endif()
include(VulkanVersion)

if (WYRM_USE_SYSTEM_BOOST AND NEED_COMMON)
    if (WYRM_BUILD_DISSECTOR OR WYRM_BUILD_CLIENT)
        find_package(Boost 1.84.0 REQUIRED COMPONENTS locale url iostreams)
    else()
        find_package(Boost 1.75.0 REQUIRED COMPONENTS locale url iostreams)
    endif()
endif()

if (WYRM_BUILD_SERVER)
    find_program(GDBUS_CODEGEN gdbus-codegen REQUIRED)
    if (Vulkan_VERSION  VERSION_LESS "1.4.304")
        message(FATAL_ERROR "Vulkan version must be at least 1.4.304, found ${Vulkan_VERSION}")
    endif()

    if (NOT WYRM_USE_NVENC AND NOT WYRM_USE_VAAPI AND NOT WYRM_USE_X264 AND NOT WYRM_USE_VULKAN_ENCODE)
        message(FATAL_ERROR "No encoder selected, use at least one of WYRM_USE_NVENC, WYRM_USE_VAAPI, WYRM_USE_VULKAN_ENCODE or WYRM_USE_X264")
    endif()

    if (WYRM_USE_VAAPI)
        pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavcodec libavutil)
        pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
    endif()

    if (WYRM_USE_X264)
        pkg_check_modules(X264 REQUIRED IMPORTED_TARGET x264)
    endif()

    pkg_check_modules(AVAHI REQUIRED IMPORTED_TARGET avahi-client avahi-glib)
    find_package(Eigen3 REQUIRED)
    find_package(nlohmann_json REQUIRED)
    find_package(CLI11 REQUIRED)

    pkg_check_modules(glib2 REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0 gio-unix-2.0)

    pkg_check_modules(libnotify REQUIRED IMPORTED_TARGET libnotify)
endif()

if (WYRM_BUILD_DASHBOARD OR WYRM_BUILD_SERVER)
    pkg_check_modules(librsvg-2.0 REQUIRED IMPORTED_TARGET librsvg-2.0)
    find_package(LibArchive REQUIRED)
    find_package(PNG REQUIRED)
endif()

if (WYRM_BUILD_DASHBOARD)
    find_package(Qt6 REQUIRED COMPONENTS Core Quick Test Gui QuickControls2 Widgets Network)

    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10.0)
        # QtCoro needs this for Qt 6.10 and (currently) doesn't do this by itself
        find_package(Qt6 REQUIRED COMPONENTS QmlPrivate)
    endif()

    # Prevent KDE cmake module from overwriting defaults
    set(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT FALSE)
    find_package(ECM 5.115 REQUIRED NO_MODULE)
    list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")

    find_package(KF6 REQUIRED COMPONENTS Kirigami I18n CoreAddons QQC2DesktopStyle IconThemes)
    find_package(KF6Kirigami)
    find_package(nlohmann_json REQUIRED)
    find_package(QCoro6 REQUIRED COMPONENTS Core Network Qml)
    qcoro_enable_coroutines()
endif()

if (WYRM_BUILD_CLIENT OR WYRM_BUILD_DASHBOARD)
    find_package(Gettext REQUIRED)
endif()

if (WYRM_USE_PIPEWIRE AND (WYRM_BUILD_SERVER OR (WYRM_BUILD_CLIENT AND NOT ANDROID)))
    pkg_check_modules(libpipewire REQUIRED IMPORTED_TARGET libpipewire-0.3)
endif()

if (WYRM_BUILD_CLIENT)
    find_program(RSVG_CONVERT rsvg-convert REQUIRED)
    find_program(KTX ktx REQUIRED)

    if (WYRM_COMPRESS_GLB)
        find_program(GLTF_TRANSFORM gltf-transform REQUIRED)
    endif()

    if(NOT ANDROID)
        find_package(CURL REQUIRED COMPONENTS HTTP HTTPS)
    endif()
endif()

if (WYRM_BUILD_CLIENT AND NOT ANDROID)
    if(NOT WYRM_USE_V4L2)
        pkg_check_modules(LIBAV_CLIENT REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale)
    endif()
    find_package(Fontconfig REQUIRED)

    if (WYRM_USE_SYSTEM_OPENXR)
        find_package(OpenXR 1.1.59 REQUIRED)
    endif()

    if (WYRM_USE_SYSTEM_LIBKTX)
        find_package(Ktx REQUIRED)
    endif()
endif()

if(ANDROID)
    set(ANDROID_NDK $ENV{ANDROID_NDK_ROOT})
    set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_ROOT})
    include(AndroidNdkModules)
    android_ndk_import_module_native_app_glue()
endif()

if (WYRM_BUILD_DISSECTOR)
    if (WYRM_USE_SYSTEM_OPENXR)
        find_package(OpenXR 1.1.53 REQUIRED)
    endif()
    find_package(Wireshark REQUIRED)
endif()

if (WYRM_BUILD_WYRMCTL)
    find_package(CLI11 REQUIRED)
    pkg_search_module(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd libelogind)
endif()

# Common dependencies
FetchContent_Declare(boost         EXCLUDE_FROM_ALL SYSTEM URL https://github.com/boostorg/boost/releases/download/boost-1.89.0/boost-1.89.0-cmake.tar.xz
                                   URL_HASH SHA256=67acec02d0d118b5de9eb441f5fb707b3a1cdd884be00ca24b9a73c995511f74)

# Client dependencies
FetchContent_Declare(simdjson      EXCLUDE_FROM_ALL SYSTEM URL https://github.com/simdjson/simdjson/archive/refs/tags/v4.2.2.tar.gz
                                   URL_HASH SHA256=3efae22cb41f83299fe0b2e8a187af543d3dda93abbb910586f897df670f9eaa)
FetchContent_Declare(fastgltf      EXCLUDE_FROM_ALL SYSTEM URL https://github.com/spnda/fastgltf/archive/refs/tags/v0.9.0.tar.gz
                                   URL_HASH SHA256=0bb564e127b14c22f062db50f89381dd2e0a20dbaf4987ca138a4ae8728712f9)
FetchContent_Declare(glm           EXCLUDE_FROM_ALL SYSTEM URL https://github.com/g-truc/glm/releases/download/1.0.2/glm-1.0.2.zip
                                   URL_HASH SHA256=77fff075c5ab0caf076fade41558c5458d9a242aba2b2076bb72008bdb51a35e)
FetchContent_Declare(openxr_loader EXCLUDE_FROM_ALL SYSTEM URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.59.tar.gz
                                   URL_HASH SHA256=d45d12d71cbe381a6fc510ec95ec84a88fe30724674e63bb038dfc9a481d8885)
FetchContent_Declare(spdlog        EXCLUDE_FROM_ALL SYSTEM URL https://github.com/gabime/spdlog/archive/refs/tags/v1.16.0.tar.gz
                                   URL_HASH SHA256=8741753e488a78dd0d0024c980e1fb5b5c85888447e309d9cb9d949bdb52aa3e)
FetchContent_Declare(stb           EXCLUDE_FROM_ALL SYSTEM URL https://github.com/nothings/stb/archive/013ac3beddff3dbffafd5177e7972067cd2b5083.zip
                                   URL_HASH SHA256=b7f476902bbef1b30f8ecc2d9d95c459c32302c8b559d09b589b5955463b7af8)
set(KTX_VERSION "4.4.2")
set(KTX_VERSION_FULL "v${KTX_VERSION}")
FetchContent_Declare(libktx        EXCLUDE_FROM_ALL SYSTEM URL https://github.com/KhronosGroup/KTX-Software/archive/refs/tags/${KTX_VERSION_FULL}.tar.gz
                                   URL_HASH SHA256=9412cb45045a503005acd47d98f9e8b47154634a50b4df21e17a1dfa8971d323
                                   )
FetchContent_Declare(implot        EXCLUDE_FROM_ALL SYSTEM URL https://github.com/epezent/implot/releases/download/v1.0/implot-v1.0.tar.gz
                                   URL_HASH SHA256=20ca14d5a46f36a650746096c74eba8bea99c9432cd3c877343b96b17a6ae566)
file(GLOB IMGUI_PATCHES CONFIGURE_DEPENDS patches/imgui/*)
FetchContent_Declare(imgui         EXCLUDE_FROM_ALL SYSTEM URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9.tar.gz
                                   URL_HASH SHA256=af97ed649182c39314320514a672b82008ab462b9293fe23d37b30bfa5d05519
                                   PATCH_COMMAND    ${CMAKE_CURRENT_LIST_DIR}/patches/apply.sh ${IMGUI_PATCHES}
)
FetchContent_Declare(uni-algo      EXCLUDE_FROM_ALL SYSTEM URL https://github.com/uni-algo/uni-algo/archive/v1.2.0.tar.gz
                                   URL_HASH SHA256=f2a1539cd8635bc6088d05144a73ecfe7b4d74ee0361fabed6f87f9f19e74ca9)
FetchContent_Declare(entt          EXCLUDE_FROM_ALL SYSTEM URL https://github.com/skypjack/entt/archive/refs/tags/v4.0.0.tar.gz
                                   URL_HASH SHA256=32a2ff2c72cb047dfd57306006ef238820b70da7c6ce4e7e8a507ac63365212e)
FetchContent_Declare(curl          EXCLUDE_FROM_ALL SYSTEM URL https://github.com/curl/curl/releases/download/curl-8_17_0/curl-8.17.0.tar.xz
                                   URL_HASH SHA256=955f6e729ad6b3566260e8fef68620e76ba3c31acf0a18524416a185acf77992)
FetchContent_Declare(spirv-reflect EXCLUDE_FROM_ALL SYSTEM URL https://github.com/KhronosGroup/SPIRV-Reflect/archive/refs/tags/vulkan-sdk-1.4.328.1.tar.gz
                                   URL_HASH SHA256=77f4b5b5630d960d7c17e39ddd5bf2e7dd62e6d8da24259ed14ca5b59783ea35)

file(GLOB MONADO_PATCHES CONFIGURE_DEPENDS patches/monado/*)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS monado-rev)
file(STRINGS monado-rev MONADO_REV)
# Server dependencies
FetchContent_Declare(monado
    GIT_REPOSITORY   https://gitlab.freedesktop.org/monado/monado.git
    # The flatpak manifest is updated automatically
    GIT_TAG          ${MONADO_REV}
    PATCH_COMMAND    ${CMAKE_CURRENT_LIST_DIR}/patches/apply.sh ${MONADO_PATCHES}
    EXCLUDE_FROM_ALL
)

add_custom_target(wyrm-version ALL
    COMMAND ${CMAKE_COMMAND}
        -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
        -D INPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/common/version.cpp.in
        -D OUTPUT_FILE=${CMAKE_BINARY_DIR}/version.cpp
        -D INPUT_FILE_1=${CMAKE_CURRENT_SOURCE_DIR}/server/u_git_tag.cpp.in
        -D OUTPUT_FILE_1=${CMAKE_BINARY_DIR}/u_git_tag.cpp
        -D GIT_TAG="${GIT_TAG}"
        -D GIT_DESC="${GIT_DESC}"
        -D GIT_COMMIT="${GIT_COMMIT}"
        -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
        BYPRODUCTS ${CMAKE_BINARY_DIR}/version.cpp ${CMAKE_BINARY_DIR}/u_git_tag.cpp
        SOURCES common/version.cpp.in server/u_git_tag.cpp.in
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

add_subdirectory(external)
if (NEED_COMMON)
    add_subdirectory(common)
endif()

if (WYRM_BUILD_CLIENT)
    add_subdirectory(client)
endif()

if (WYRM_BUILD_SERVER OR WYRM_BUILD_SERVER_LIBRARY)
    find_package(Python3 REQUIRED Interpreter)
    add_subdirectory(server)

    if (WYRM_BUILD_SERVER)
        message(STATUS "Selected encoders:")
        message(STATUS "\tNVENC : ${WYRM_USE_NVENC}")
        message(STATUS "\tVAAPI : ${WYRM_USE_VAAPI}")
        message(STATUS "\tVulkan: ${WYRM_USE_VULKAN_ENCODE}")
        message(STATUS "\tx264  : ${WYRM_USE_X264}")
        message(STATUS "")
        message(STATUS "Audio backends:")
        message(STATUS "\tPipewire  : ${WYRM_USE_PIPEWIRE}")
    endif()
endif()

if (WYRM_BUILD_DASHBOARD)
    add_subdirectory(dashboard)
    set_target_properties(wyrm-dashboard PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/server)
endif()

add_subdirectory(tools)

foreach(TARGET_NAME wyrm wyrm-server wyrm-dashboard wyrm-common wyrm-dissector wyrmctl)
    if(TARGET ${TARGET_NAME})
        target_compile_options(${TARGET_NAME} PRIVATE
            -fdiagnostics-color -Wall -Wextra -pedantic
            -ffast-math
            -Wundef
            -Wno-missing-field-initializers
            -Wno-unused-parameter
            -Wno-comment
            -Wno-sign-compare
            $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-volatile -Wno-array-bounds>
            $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-deprecated-volatile -Wno-unneeded-internal-declaration -Wno-c99-extensions -Wno-unused-private-field>
        )

        if (WYRM_WERROR)
            target_compile_options(${TARGET_NAME} PRIVATE -Werror)
        endif()

        if (WYRM_ADDRESS_SANITIZER AND NOT ${TARGET_NAME} STREQUAL wyrm-dissector)
            target_compile_options(${TARGET_NAME} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
            target_link_options(${TARGET_NAME} PRIVATE -fsanitize=address)
        endif()

    endif()
endforeach()
