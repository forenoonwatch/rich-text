cmake_minimum_required(VERSION 3.14)

project(
	ICUTest VERSION 1.0
	LANGUAGES C CXX
)

#list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_subdirectory(third_party)

add_executable(ICUTest "")
target_sources(ICUTest PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/font_instance.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/bitmap.cpp"
)

target_link_libraries(ICUTest PRIVATE ICU::common ICU::layoutex)
target_link_libraries(ICUTest PRIVATE minifb)
set_target_properties(ICUTest PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

add_custom_command(TARGET ICUTest POST_BUILD
	COMMAND
		${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ICU::data> $<TARGET_FILE_DIR:ICUTest>
)
