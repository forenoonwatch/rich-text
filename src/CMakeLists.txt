target_sources(LibRichText PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/file_mapping.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/font_registry.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/font_registry_json.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/font_data.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/formatting.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/formatting_iterator.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/harfbuzz_font.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/layout_builder.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/layout_info.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/script_run_iterator.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/cursor_controller.cpp"
)

target_include_directories(LibRichText PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
