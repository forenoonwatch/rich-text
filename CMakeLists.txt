cmake_minimum_required(VERSION 3.14)

project(
	ICUTest VERSION 1.0
	LANGUAGES C CXX
)

add_subdirectory(third_party)

add_executable(ICUTest "")

target_link_libraries(ICUTest PRIVATE ICU::common ICU::layoutex)
target_link_libraries(ICUTest PRIVATE msdfgen::msdfgen)
target_link_libraries(ICUTest PRIVATE minifb)
target_link_libraries(ICUTest PRIVATE simdjson)
set_target_properties(ICUTest PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

add_custom_command(TARGET ICUTest POST_BUILD
	COMMAND
		${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ICU::data> $<TARGET_FILE_DIR:ICUTest>
)

add_subdirectory(fonts)
add_subdirectory(src)
