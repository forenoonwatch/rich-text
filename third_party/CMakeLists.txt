set(GLFW_BUILD_DOCS OFF CACHE BOOL "")
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "")
set(GLFW_BUILD_TESTS OFF CACHE BOOL "")
set(GLFW_INSTALL OFF CACHE BOOL "")

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
add_subdirectory(glfw)
add_subdirectory(harfbuzz)
add_subdirectory(icu_cmake)
add_subdirectory(msdfgen)
add_subdirectory(simdjson)

add_library(glad STATIC "")
target_sources(glad PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/glad/src/glad.c")
target_include_directories(glad PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/glad/include")

if (MINGW)
	target_compile_options(harfbuzz PRIVATE -Wa,-mbig-obj)
	target_compile_options(harfbuzz-subset PRIVATE -Wa,-mbig-obj)
endif()

add_library(icu-le-hb STATIC "")
file(GLOB_RECURSE ICU_LE_HB_FILES "${CMAKE_CURRENT_SOURCE_DIR}/icu-le-hb/src/*.cpp")
target_sources(icu-le-hb PRIVATE ${ICU_LE_HB_FILES})
target_link_libraries(icu-le-hb PUBLIC ICU::common ICU::data harfbuzz)

file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/icu-le-hb/src" "${CMAKE_CURRENT_BINARY_DIR}/layout" SYMBOLIC)
target_include_directories(icu-le-hb PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
