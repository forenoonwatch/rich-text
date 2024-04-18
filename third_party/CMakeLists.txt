include(FetchContent)

set(ICU_CHARSET_IS_UTF8 ON CACHE BOOL "")

set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "")
set(MSDFGEN_CORE_ONLY ON CACHE BOOL "")
set(MSDFGEN_DYNAMIC_RUNTIME OFF CACHE BOOL "")
set(MSDFGEN_INSTALL OFF CACHE BOOL "")
set(MSDFGEN_USE_CPP11 ON CACHE BOOL "")
set(MSDFGEN_USE_OPENMP OFF CACHE BOOL "")
set(MSDFGEN_USE_SKIA OFF CACHE BOOL "")
set(MSDFGEN_USE_VCPKG OFF CACHE BOOL "")

add_subdirectory(freetype)
add_subdirectory(harfbuzz)
add_subdirectory(icu_cmake)
add_subdirectory(msdfgen)

if (MINGW)
	target_compile_options(harfbuzz PRIVATE -Wa,-mbig-obj)
	target_compile_options(harfbuzz-subset PRIVATE -Wa,-mbig-obj)
endif()

if (NOT TARGET simdjson)
	FetchContent_Declare(
		simdjson
		GIT_REPOSITORY https://github.com/simdjson/simdjson.git
	)

	FetchContent_MakeAvailable(simdjson)
endif()

if (RICHTEXT_BUILD_TESTS)
	add_library(richtext_glad STATIC "")
	target_sources(richtext_glad PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/glad/src/glad.c")
	target_include_directories(richtext_glad PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/glad/include")

	if (NOT TARGET glfw)
		set(GLFW_BUILD_DOCS OFF CACHE BOOL "")
		set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "")
		set(GLFW_BUILD_TESTS OFF CACHE BOOL "")
		set(GLFW_INSTALL OFF CACHE BOOL "")

		FetchContent_Declare(
			glfw
			GIT_REPOSITORY https://github.com/glfw/glfw.git
		)

		FetchContent_MakeAvailable(glfw)
	endif()

	if (NOT TARGET Catch2::Catch2WithMain)
		FetchContent_Declare(
			Catch2
			GIT_REPOSITORY https://github.com/catchorg/Catch2.git
			GIT_TAG v3.5.4
		)

		FetchContent_MakeAvailable(Catch2)
	endif()

	if (NOT TARGET benchmark::benchmark)
		set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "")

		FetchContent_Declare(
			benchmark
			GIT_REPOSITORY https://github.com/google/benchmark.git
			GIT_TAG v1.8.3
		)

		FetchContent_MakeAvailable(benchmark)
	endif()
endif()

add_library(SheenBidi STATIC)
target_sources(SheenBidi PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/BidiChain.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/BidiTypeLookup.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/BracketQueue.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/GeneralCategoryLookup.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/IsolatingRun.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/LevelRun.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/PairingLookup.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/RunQueue.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/SBAlgorithm.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/SBBase.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/SBCodepointSequence.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/SBLine.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/SBLog.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/SBMirrorLocator.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/SBParagraph.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/SBScriptLocator.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/ScriptLookup.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/ScriptStack.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/SheenBidi.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Source/StatusStack.c"
)
target_include_directories(SheenBidi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/SheenBidi/Headers")
