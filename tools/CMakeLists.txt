if(WYRM_BUILD_DISSECTOR)
	add_subdirectory(wireshark)
endif()

if (WYRM_BUILD_WYRMCTL)
	add_subdirectory(wyrmctl)
endif()
