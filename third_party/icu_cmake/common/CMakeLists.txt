file(REAL_PATH "../icu/icu4c/source" ICU_SOURCE_DIR BASE_DIRECTORY ${PROJECT_SOURCE_DIR})

add_library(ICU_common STATIC)
set_target_properties(ICU_common PROPERTIES
	EXPORT_NAME ${COMMON_STUBNAME}
	OUTPUT_NAME
		${STATIC_PREFIX}${ICULIBS_UC}${ICULIBSUFFIX_DEBUG}
	RUNTIME_OUTPUT_NAME
		${STATIC_PREFIX}${ICULIBS_UC}${ICULIBSUFFIX_VERSION}${ICULIBSUFFIX_DEBUG}
)
target_compile_definitions(ICU_common PRIVATE U_COMMON_IMPLEMENTATION)
file(GLOB_RECURSE ICU_COMMON_SOURCE_FILES "${ICU_SOURCE_DIR}/common/*.cpp")
target_sources(ICU_common PRIVATE ${ICU_COMMON_SOURCE_FILES})
target_include_directories(ICU_common PUBLIC "${ICU_SOURCE_DIR}/common")

if (ICU_CHARSET_IS_UTF8)
	target_compile_definitions(ICU_common PUBLIC U_CHARSET_IS_UTF8=1)
endif()

add_library(ICU::common ALIAS ICU_common)
