add_library(wyrm-external INTERFACE)
target_include_directories(wyrm-external SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# add_library(imspinner OBJECT EXCLUDE_FROM_ALL imspinner/cimspinner.cpp)
add_library(imspinner INTERFACE)
target_include_directories(imspinner SYSTEM INTERFACE imspinner)
target_link_libraries(imspinner INTERFACE wyrm-imgui)
