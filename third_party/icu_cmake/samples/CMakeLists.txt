file(REAL_PATH "../icu/icu4c/source" ICU_SOURCE_DIR BASE_DIRECTORY ${PROJECT_SOURCE_DIR})

# layout
add_executable(layout
	"${ICU_SOURCE_DIR}/samples/layout/layout.cpp"
	"${ICU_SOURCE_DIR}/samples/layout/GDIFontInstance.cpp"
	"${ICU_SOURCE_DIR}/samples/layout/GDIFontMap.cpp"
	"${ICU_SOURCE_DIR}/samples/layout/gdiglue.cpp"
	"${ICU_SOURCE_DIR}/samples/layout/GDIGUISupport.cpp"
	"${ICU_SOURCE_DIR}/samples/layout/paragraph.cpp"
	"${ICU_SOURCE_DIR}/samples/layout/FontMap.cpp"
	"${ICU_SOURCE_DIR}/samples/layout/FontTableCache.cpp"
	"${ICU_SOURCE_DIR}/samples/layout/ScriptCompositeFontInstance.cpp"
	"${ICU_SOURCE_DIR}/samples/layout/UnicodeReader.cpp"
	"${ICU_SOURCE_DIR}/samples/layout/cmaps.cpp"
)

target_link_libraries(layout PUBLIC ICU::common ICU::data ICU::layoutex)
target_include_directories(layout PUBLIC "${ICU_SOURCE_DIR}/samples/layout")

add_custom_command(TARGET layout POST_BUILD
	COMMAND
		${CMAKE_COMMAND} -E copy_if_different "${PROJECT_BINARY_DIR}/data/libicudt.dll" $<TARGET_FILE_DIR:layout>
)