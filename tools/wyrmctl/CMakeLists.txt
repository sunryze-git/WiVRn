add_executable(wyrmctl main.cpp)
set_target_properties(wyrmctl PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/server)
target_link_libraries(wyrmctl PRIVATE CLI11::CLI11 PkgConfig::SYSTEMD)

install(TARGETS wyrmctl)

install(FILES dist/wyrmctl.bash
	RENAME wyrmctl
	DESTINATION share/bash-completion/completions)

install(FILES dist/_wyrmctl.zsh
	RENAME _wyrmctl
	DESTINATION share/zsh/site-functions)
