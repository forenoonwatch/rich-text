target_sources(TestRichText PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/bidi_test_data.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/test_script_runs.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/test_bidi.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/test_paragraph_layout.cpp"
)

target_include_directories(TestRichText PRIVATE "${PROJECT_SOURCE_DIR}/src")

# FIXME: Library l o l 
target_sources(TestRichText PRIVATE
	"${PROJECT_SOURCE_DIR}/src/bitmap.cpp"
	"${PROJECT_SOURCE_DIR}/src/font.cpp"
	"${PROJECT_SOURCE_DIR}/src/font_cache.cpp"
	"${PROJECT_SOURCE_DIR}/src/multi_script_font.cpp"
	"${PROJECT_SOURCE_DIR}/src/rich_text.cpp"
	"${PROJECT_SOURCE_DIR}/src/paragraph_layout.cpp"
	"${PROJECT_SOURCE_DIR}/src/build_paragraph_layout_icu.cpp"
	"${PROJECT_SOURCE_DIR}/src/convert_paragraph_layout_utf8.cpp"
)
