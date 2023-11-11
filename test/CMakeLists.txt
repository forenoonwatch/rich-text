target_sources(TestRichText PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/bidi_test_data.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/test_script_runs.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/test_bidi.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/test_bidi_utf8.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/test_sheen_bidi.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/test_paragraph_layout.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/u16on8bidi.cpp"
)

target_sources(TestRichText PRIVATE
	"${PROJECT_SOURCE_DIR}/src/u8bidi.cpp"
	"${PROJECT_SOURCE_DIR}/src/u8bidiln.cpp"
	"${PROJECT_SOURCE_DIR}/src/u8bidiwrt.cpp"
)

target_sources(BenchRichText PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/bench_bidi.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/bidi_test_data.cpp"
)

