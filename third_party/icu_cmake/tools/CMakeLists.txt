file(REAL_PATH "../icu/icu4c/source" ICU_SOURCE_DIR BASE_DIRECTORY ${PROJECT_SOURCE_DIR})

add_library(ICU_toolutil STATIC)
set_target_properties(ICU_toolutil PROPERTIES
	EXPORT_NAME ${TOOLUTIL_STUBNAME}
	OUTPUT_NAME
		${STATIC_PREFIX}${ICULIBS_TOOLUTIL}${ICULIBSUFFIX_DEBUG}
	RUNTIME_OUTPUT_NAME
		${STATIC_PREFIX}${ICULIBS_TOOLUTIL}${ICULIBSUFFIX_VERSION}${ICULIBSUFFIX_DEBUG}
)
target_compile_definitions(ICU_toolutil PRIVATE U_TOOLUTIL_IMPLEMENTATION)
file(GLOB_RECURSE ICU_TOOLS_TOOLUTIL_SOURCE_FILES "${ICU_SOURCE_DIR}/tools/toolutil/*.cpp")
target_sources(ICU_toolutil PRIVATE ${ICU_TOOLS_TOOLUTIL_SOURCE_FILES})
target_include_directories(ICU_toolutil PUBLIC "${ICU_SOURCE_DIR}/tools/toolutil")
target_link_libraries(ICU_toolutil PUBLIC ICU_common ICU_i18n)

add_library(ICU::toolutil ALIAS ICU_toolutil)

add_custom_command(TARGET ICU_toolutil POST_BUILD
	COMMAND
		${CMAKE_COMMAND} -E copy_if_different "${PROJECT_BINARY_DIR}/stubdata/libicudt.dll" $<TARGET_FILE_DIR:ICU_toolutil>
)

# pkgdata
add_executable(pkgdata
	"${ICU_SOURCE_DIR}/tools/pkgdata/pkgdata.cpp"
	"${ICU_SOURCE_DIR}/tools/pkgdata/pkgtypes.c"
)
target_link_libraries(pkgdata PUBLIC ICU_toolutil ICU_stubdata)


add_executable(ICU::pkgdata ALIAS pkgdata)

# icupkg
add_executable(icupkg "${ICU_SOURCE_DIR}/tools/icupkg/icupkg.cpp")
target_link_libraries(icupkg PUBLIC ICU_toolutil ICU_stubdata)

add_executable(ICU::icupkg ALIAS icupkg)

# ctestfw
if (ICU_BUILD_TESTS)
	add_library(ICU_ctestfw STATIC)
	set_target_properties(ICU_ctestfw PROPERTIES
		EXPORT_NAME ${CTESTFW_STUBNAME}
		OUTPUT_NAME
			${STATIC_PREFIX}${ICULIBS_CTESTFW}${ICULIBSUFFIX_DEBUG}
		RUNTIME_OUTPUT_NAME
			${STATIC_PREFIX}${ICULIBS_CTESTFW}${ICULIBSUFFIX_VERSION}${ICULIBSUFFIX_DEBUG}
	)
	target_compile_definitions(ICU_ctestfw PRIVATE T_CTEST_IMPLEMENTATION)
	target_sources(ICU_ctestfw PRIVATE
		"${ICU_SOURCE_DIR}/tools/ctestfw/ctest.c"
		"${ICU_SOURCE_DIR}/tools/ctestfw/datamap.cpp"
		"${ICU_SOURCE_DIR}/tools/ctestfw/testdata.cpp"
		"${ICU_SOURCE_DIR}/tools/ctestfw/tstdtmod.cpp"
		"${ICU_SOURCE_DIR}/tools/ctestfw/ucln_ct.c"
		"${ICU_SOURCE_DIR}/tools/ctestfw/uperf.cpp"
	)
	target_include_directories(ICU_ctestfw PUBLIC "${ICU_SOURCE_DIR}/tools/ctestfw")
	target_link_libraries(ICU_ctestfw PUBLIC ICU_common ICU_i18n ICU_toolutil)

	add_library(ICU::ctestfw ALIAS ICU_ctestfw)
endif()

