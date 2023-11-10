cmake_minimum_required(VERSION 3.14)

project(
	RichText VERSION 1.0
	LANGUAGES C CXX
)

add_subdirectory(third_party)

# RichText Sample Program ##########################################################################

add_executable(RichText "")

target_link_libraries(RichText PRIVATE glad)
target_link_libraries(RichText PRIVATE glfw)
target_link_libraries(RichText PRIVATE ICU::common ICU::layoutex)
target_link_libraries(RichText PRIVATE msdfgen::msdfgen)
target_link_libraries(RichText PRIVATE simdjson)
set_target_properties(RichText PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

add_custom_command(TARGET RichText POST_BUILD
	COMMAND
		${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ICU::data> $<TARGET_FILE_DIR:RichText>
)

# RichText Tester ##################################################################################

add_executable(TestRichText "")
target_link_libraries(TestRichText PRIVATE ICU::common ICU::layoutex)
target_link_libraries(TestRichText PRIVATE Catch2::Catch2WithMain)
target_link_libraries(TestRichText PRIVATE msdfgen::msdfgen)
target_link_libraries(TestRichText PRIVATE simdjson)
target_link_libraries(TestRichText PRIVATE SheenBidi)
set_target_properties(TestRichText PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

add_custom_command(TARGET TestRichText POST_BUILD
	COMMAND
		${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ICU::data> $<TARGET_FILE_DIR:TestRichText>
)

# RichText Benchmark ################################################################################

add_executable(BenchRichText "")
target_link_libraries(BenchRichText PRIVATE ICU::common)
target_link_libraries(BenchRichText PRIVATE benchmark::benchmark)
target_link_libraries(BenchRichText PRIVATE SheenBidi)
set_target_properties(BenchRichText PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

add_custom_command(TARGET BenchRichText POST_BUILD
	COMMAND
		${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ICU::data> $<TARGET_FILE_DIR:BenchRichText>
)

add_subdirectory(fonts)
add_subdirectory(src)
add_subdirectory(test)
