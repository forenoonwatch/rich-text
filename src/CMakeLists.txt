target_sources(LibRichText PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/bitmap.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/font.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/font_cache.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/formatting.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/formatting_iterator.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/multi_script_font.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/layout_info.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/build_layout_info_lx.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/build_layout_info_icu.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/build_layout_info_utf8.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/convert_layout_info_utf8.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/script_run_iterator.cpp"
)

target_include_directories(LibRichText PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
