cmake_minimum_required(VERSION 3.14)

project(
	RichText VERSION 1.0
	LANGUAGES C CXX
)

option(RICHTEXT_BUILD_TESTS "Build Tests, Benchmarks, and Samples" OFF)

add_subdirectory(third_party)

if (RICHTEXT_BUILD_TESTS)
	if (NOT ICU_BUILD_LX)
		message(SEND_ERROR "LibRichText: Enable ICU_BUILD_LX option to build tests")
	endif()
endif()

# LibRichText ######################################################################################

add_library(LibRichText STATIC "")
target_link_libraries(LibRichText PUBLIC ICU::common ICU::data harfbuzz)
target_link_libraries(LibRichText PRIVATE SheenBidi)
target_link_libraries(LibRichText PRIVATE simdjson)

if (ICU_BUILD_LX)
	target_link_libraries(LibRichText PRIVATE ICU::layoutex)
endif()

set_target_properties(LibRichText PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
	INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
)

add_custom_command(TARGET LibRichText POST_BUILD
	COMMAND
		${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ICU::data> $<TARGET_FILE_DIR:LibRichText>
)

# RichText Sample Program ##########################################################################

if (RICHTEXT_BUILD_TESTS)
	add_executable(RichText "")

	target_link_libraries(RichText PRIVATE LibRichText)
	target_link_libraries(RichText PRIVATE msdfgen::msdfgen)
	target_link_libraries(RichText PRIVATE richtext_glad)
	target_link_libraries(RichText PRIVATE glfw)
	set_target_properties(RichText PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
		INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
	)
endif()

# RichText Tester ##################################################################################

if (RICHTEXT_BUILD_TESTS)
	add_executable(TestRichText "")
	target_link_libraries(TestRichText PRIVATE LibRichText)
	target_link_libraries(TestRichText PRIVATE Catch2::Catch2WithMain)
	target_link_libraries(TestRichText PRIVATE SheenBidi)
	set_target_properties(TestRichText PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
		INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
	)

	add_custom_command(TARGET TestRichText POST_BUILD
		COMMAND
			${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ICU::data> $<TARGET_FILE_DIR:TestRichText>
	)
endif()

# RichText Benchmark ################################################################################

if (RICHTEXT_BUILD_TESTS)
	add_executable(BenchRichText "")
	target_link_libraries(BenchRichText PRIVATE LibRichText)
	target_link_libraries(BenchRichText PRIVATE benchmark::benchmark)
	target_link_libraries(BenchRichText PRIVATE SheenBidi)
	set_target_properties(BenchRichText PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
		CXX_EXTENSIONS OFF
		INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
	)

	add_custom_command(TARGET BenchRichText POST_BUILD
		COMMAND
			${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ICU::data> $<TARGET_FILE_DIR:BenchRichText>
	)
endif()

add_subdirectory(src)

if (RICHTEXT_BUILD_TESTS)
	add_subdirectory(fonts)
	add_subdirectory(sample)
	add_subdirectory(test)
endif()
