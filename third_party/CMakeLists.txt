add_subdirectory(freetype)
add_subdirectory(harfbuzz)
add_subdirectory(icu_cmake)
add_subdirectory(minifb)

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