target_sources(TestRichText PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/bidi_test_data.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/test_script_runs.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/test_bidi.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/test_sheen_bidi.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/test_layout_info.cpp"
)

target_sources(BenchRichText PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/bench_bidi.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/bench_layout.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/bidi_test_data.cpp"
)

