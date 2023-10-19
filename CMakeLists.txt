cmake_minimum_required(VERSION 3.14)

project(
	RichText VERSION 1.0
	LANGUAGES C CXX
)

add_subdirectory(third_party)

add_executable(RichText "")

target_link_libraries(RichText PRIVATE glad)
target_link_libraries(RichText PRIVATE glfw)
target_link_libraries(RichText PRIVATE ICU::common ICU::layoutex)
target_link_libraries(RichText PRIVATE msdfgen::msdfgen)
target_link_libraries(RichText PRIVATE simdjson)
target_link_libraries(RichText PRIVATE stb)
set_target_properties(RichText PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

add_custom_command(TARGET RichText POST_BUILD
	COMMAND
		${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ICU::data> $<TARGET_FILE_DIR:RichText>
)

add_subdirectory(fonts)
add_subdirectory(src)
