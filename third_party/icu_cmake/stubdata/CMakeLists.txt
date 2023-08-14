file(REAL_PATH "../icu/icu4c/source" ICU_SOURCE_DIR BASE_DIRECTORY ${PROJECT_SOURCE_DIR})

add_library(ICU_stubdata SHARED)
set_target_properties(ICU_stubdata PROPERTIES
	EXPORT_NAME ${DATA_STUBNAME}
	OUTPUT_NAME
		${STATIC_PREFIX}${ICULIBS_DT}${ICULIBSUFFIX_DEBUG}
	RUNTIME_OUTPUT_NAME
		${STATIC_PREFIX}${ICULIBS_DT}${ICULIBSUFFIX_VERSION}${ICULIBSUFFIX_DEBUG}
)
target_sources(ICU_stubdata PRIVATE "${ICU_SOURCE_DIR}/stubdata/stubdata.cpp")
target_include_directories(ICU_stubdata PUBLIC "${ICU_SOURCE_DIR}/common" "${ICU_SOURCE_DIR}/stubdata")

if (MSVC)
	set_property(TARGET ICU_stubdata APPEND PROPERTY COMPILE_DEFINITIONS STUBDATA_BUILD)
endif()

add_library(ICU::stubdata ALIAS ICU_stubdata)