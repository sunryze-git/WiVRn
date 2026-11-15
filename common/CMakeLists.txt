if (WYRM_USE_SYSTEM_BOOST)
    add_library(Boost::pfr ALIAS Boost::headers)
else()
    set(BOOST_INCLUDE_LIBRARIES locale url pfr iostreams)
    set(BOOST_LOCALE_ENABLE_ICU OFF)
    FetchContent_MakeAvailable(boost)
endif()

configure_file(wyrm_config.h.in wyrm_config.h)

if(WYRM_BUILD_TEST)
    add_executable(test-link-policy test_link_policy.cpp)
    target_link_libraries(test-link-policy PRIVATE wyrm-common)
    add_test(NAME link-policy COMMAND test-link-policy)
    add_executable(benchmark-multilink benchmark_multilink.cpp)
    target_link_libraries(benchmark-multilink PRIVATE wyrm-common)
    add_test(NAME multilink-backlog COMMAND benchmark-multilink backlog multi 1 --verify)
    add_test(NAME multilink-backlog-video COMMAND benchmark-multilink backlog video 1 --verify)
    set_tests_properties(multilink-backlog multilink-backlog-video PROPERTIES TIMEOUT 20)
    set(WYRM_MULTILINK_BASELINE_SOURCE "" CACHE FILEPATH "Transport source snapshot for comparison benchmarks")
    if(WYRM_MULTILINK_BASELINE_SOURCE)
        get_filename_component(WYRM_MULTILINK_BASELINE_DIR ${WYRM_MULTILINK_BASELINE_SOURCE} DIRECTORY)
        add_executable(benchmark-multilink-baseline benchmark_multilink.cpp ${WYRM_MULTILINK_BASELINE_SOURCE})
        target_compile_definitions(benchmark-multilink-baseline PRIVATE WYRM_BENCH_BASELINE WYRM_BENCH_BASELINE_HEADER="${WYRM_MULTILINK_BASELINE_DIR}/multilink.h")
        target_link_libraries(benchmark-multilink-baseline PRIVATE wyrm-common)
    endif()
    add_executable(test-server-list test_server_list.cpp)
    target_link_libraries(test-server-list PRIVATE wyrm-common)
    add_test(NAME server-list COMMAND test-server-list)
    add_executable(test-multilink test_multilink.cpp)
    target_link_libraries(test-multilink PRIVATE wyrm-common)
    add_test(NAME multilink COMMAND test-multilink)
    set_tests_properties(multilink PROPERTIES TIMEOUT 45)
    add_executable(test-multilink-jitter test_multilink_jitter.cpp)
    target_link_libraries(test-multilink-jitter PRIVATE wyrm-common)
    add_test(NAME multilink-jitter-recovery COMMAND test-multilink-jitter)
    set_tests_properties(multilink-jitter-recovery PROPERTIES TIMEOUT 15)
    add_executable(test-network-impairment test_network_impairment.cpp)
    target_link_libraries(test-network-impairment PRIVATE wyrm-common)
    add_test(NAME network-impairment COMMAND test-network-impairment)
    set_tests_properties(network-impairment PROPERTIES TIMEOUT 10)
    add_executable(test-multilink-live test_multilink_live.cpp)
    target_link_libraries(test-multilink-live PRIVATE wyrm-common)
    add_executable(benchmark-multilink-live benchmark_multilink_live.cpp)
    target_link_libraries(benchmark-multilink-live PRIVATE wyrm-common)
    add_executable(test-stream-features test_stream_features.cpp)
    target_link_libraries(test-stream-features PRIVATE wyrm-common)
    add_test(NAME stream-features COMMAND test-stream-features)
    add_executable(test-video-shards test_video_shards.cpp fec.cpp shard_stream.cpp)
    target_link_libraries(test-video-shards PRIVATE wyrm-common leopard)
    target_include_directories(test-video-shards PRIVATE ${CMAKE_SOURCE_DIR}/external/leopard)
    add_test(NAME video-shards COMMAND test-video-shards)
    add_executable(test-video-stream test_video_stream.cpp)
    target_link_libraries(test-video-stream PRIVATE wyrm-common)
    add_test(NAME video-stream COMMAND test-video-stream)
endif()

add_library(wyrm-common-base STATIC EXCLUDE_FROM_ALL
    utils/strings.cpp
    utils/xdg_base_directory.cpp
    ${CMAKE_BINARY_DIR}/version.cpp
)
add_dependencies(wyrm-common-base wyrm-version)

add_library(wyrm-common STATIC EXCLUDE_FROM_ALL
    crypto.cpp
    smp.cpp
    secrets.cpp
    network.cpp
    multilink.cpp
    link_probe.cpp
    wyrm_sockets.cpp
    wyrm_foveation.cpp
    utils/strings.cpp
    vk/allocation.cpp
    vk/error_category.cpp
    vk/vk_allocator.cpp
    vk/vk_mem_alloc.cpp
)

if (WYRM_BUILD_DASHBOARD OR WYRM_BUILD_SERVER)
    add_library(wyrm-common-server STATIC EXCLUDE_FROM_ALL
        application.cpp
        utils/flatpak.cpp
        utils/load_icon.cpp
        utils/ini.cpp
        utils/steam_info.cpp
        utils/vdf.cpp
        utils/xdg_icon_lookup.cpp
    )
    target_link_libraries(wyrm-common-server
        PRIVATE nlohmann_json::nlohmann_json PNG::PNG PkgConfig::librsvg-2.0 LibArchive::LibArchive Boost::iostreams
        PUBLIC wyrm-common-base)

    if (WYRM_BUILD_TEST)
        add_executable(list-apps
            test_application.cpp
        )

        target_link_libraries(list-apps wyrm-common-server)

        add_executable(vdf utils/vdf.cpp)
        target_compile_definitions(vdf PRIVATE WYRM_BUILD_TEST)
        target_link_libraries(vdf Boost::iostreams)
    endif()
endif()

if(ANDROID)
    # The Vulkan headers in Android do not include the C++ headers, download them
    # Minimum version is 1.3.256
    set(VULKAN_VERSION "1.3.268.0")

    if(NOT EXISTS ${FETCHCONTENT_BASE_DIR}/vulkan-sdk-${VULKAN_VERSION}.tar.gz)
        if (EXISTS ${CMAKE_SOURCE_DIR}/vulkan-sdk-${VULKAN_VERSION}.tar.gz)
            file(CREATE_LINK ${CMAKE_SOURCE_DIR}/vulkan-sdk-${VULKAN_VERSION}.tar.gz ${FETCHCONTENT_BASE_DIR}/vulkan-sdk-${VULKAN_VERSION}.tar.gz SYMBOLIC)
        else()
            file(DOWNLOAD https://github.com/KhronosGroup/Vulkan-Headers/archive/vulkan-sdk-${VULKAN_VERSION}.tar.gz ${FETCHCONTENT_BASE_DIR}/vulkan-sdk-${VULKAN_VERSION}.tar.gz
                EXPECTED_HASH SHA256=94993cbe2b1a604c0d5d9ea37a767e1aba4d771d2bfd4ddceefd66243095164f)
        endif()
    endif()

    file(ARCHIVE_EXTRACT INPUT ${FETCHCONTENT_BASE_DIR}/vulkan-sdk-${VULKAN_VERSION}.tar.gz DESTINATION ${CMAKE_BINARY_DIR})

    target_include_directories(wyrm-common SYSTEM PUBLIC ${CMAKE_BINARY_DIR}/Vulkan-Headers-vulkan-sdk-${VULKAN_VERSION}/include)
    target_compile_definitions(wyrm-common PUBLIC VMA_VULKAN_VERSION=1001000)
else()
    target_link_libraries(wyrm-common PUBLIC Vulkan::Headers)
endif()

target_include_directories(wyrm-common-base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(wyrm-common PUBLIC Boost::pfr OpenSSL::Crypto wyrm-external wyrm-common-base)
target_compile_definitions(wyrm-common PUBLIC VULKAN_HPP_NO_STRUCT_CONSTRUCTORS)
