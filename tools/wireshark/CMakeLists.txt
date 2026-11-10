add_library(wyrm-dissector SHARED
	dissector.cpp
	)

set_target_properties(wyrm-dissector PROPERTIES
	PREFIX ""
	OUTPUT_NAME "wyrm"
	DEFINE_SYMBOL ""
	)

target_link_libraries(wyrm-dissector PRIVATE epan wyrm-common)
set_target_properties(wyrm-dissector PROPERTIES CXX_VISIBILITY_PRESET hidden)

if (WYRM_USE_SYSTEM_OPENXR)
	target_link_libraries(wyrm-dissector PRIVATE OpenXR::headers)
else()
	FetchContent_MakeAvailable(openxr_loader)
	get_target_property(OPENXR_LOADER_INCLUDES openxr_loader INCLUDE_DIRECTORIES)
	target_include_directories(wyrm-dissector PRIVATE ${OPENXR_LOADER_INCLUDES})
endif()

install(TARGETS wyrm-dissector
	LIBRARY DESTINATION "${Wireshark_PLUGIN_LIBDIR}/epan" NAMELINK_SKIP
)
