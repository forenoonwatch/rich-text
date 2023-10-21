file(REAL_PATH "../icu/icu4c/source" ICU_SOURCE_DIR BASE_DIRECTORY ${PROJECT_SOURCE_DIR})

# cintltst
add_executable(cintltst "")
file(GLOB_RECURSE ICU_CINTLTST_SOURCE_FILES "${ICU_SOURCE_DIR}/test/cintltst/*.c")
target_sources(cintltst PRIVATE ${ICU_CINTLTST_SOURCE_FILES})
target_include_directories(cintltst PUBLIC "${ICU_SOURCE_DIR}/test/cintltst")
target_link_libraries(cintltst PUBLIC ICU_ctestfw ICU_toolutil ICU_i18n ICU_common ICU::data)

add_custom_command(TARGET cintltst POST_BUILD
	COMMAND
		${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ICU::data> $<TARGET_FILE_DIR:cintltst>
)
