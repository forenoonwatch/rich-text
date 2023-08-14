file(REAL_PATH "../icu/icu4c/source" ICU_SOURCE_DIR BASE_DIRECTORY ${PROJECT_SOURCE_DIR})

add_library(ICU_layoutex STATIC)
set_target_properties(ICU_layoutex PROPERTIES
	EXPORT_NAME ${LAYOUTEX_STUBNAME}
	OUTPUT_NAME
		${STATIC_PREFIX}${ICULIBS_LX}${ICULIBSUFFIX_DEBUG}
	RUNTIME_OUTPUT_NAME
		${STATIC_PREFIX}${ICULIBS_LX}${ICULIBSUFFIX_VERSION}${ICULIBSUFFIX_DEBUG}
)
target_compile_definitions(ICU_layoutex PRIVATE U_TOOLUTIL_IMPLEMENTATION)
file(GLOB ICU_LAYOUTEX_SOURCE_FILES "${ICU_SOURCE_DIR}/layoutex/*.cpp")
target_sources(ICU_layoutex PRIVATE ${ICU_LAYOUTEX_SOURCE_FILES})
target_include_directories(ICU_layoutex PUBLIC "${ICU_SOURCE_DIR}/layoutex")
target_link_libraries(ICU_layoutex PUBLIC icu-le-hb)

add_library(ICU::layoutex ALIAS ICU_layoutex)