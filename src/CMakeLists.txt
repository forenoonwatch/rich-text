target_sources(LibRichText PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/bitmap.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/font.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/font_cache.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/formatting.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/multi_script_font.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/paragraph_layout.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/build_paragraph_layout_icu.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/build_paragraph_layout_utf8.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/convert_paragraph_layout_utf8.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/script_run_iterator.cpp"
)

target_include_directories(LibRichText PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
