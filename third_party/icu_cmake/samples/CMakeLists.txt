file(REAL_PATH "../icu/icu4c/source" ICU_SOURCE_DIR BASE_DIRECTORY ${PROJECT_SOURCE_DIR})

# layout
if (ICU_BUILD_LX)
	if (WIN32)
		# layout
		add_executable(layout
			"${ICU_SOURCE_DIR}/samples/layout/paragraph.cpp"
			"${ICU_SOURCE_DIR}/samples/layout/FontMap.cpp"
			"${ICU_SOURCE_DIR}/samples/layout/FontTableCache.cpp"
			"${ICU_SOURCE_DIR}/samples/layout/ScriptCompositeFontInstance.cpp"
			"${ICU_SOURCE_DIR}/samples/layout/UnicodeReader.cpp"
			"${ICU_SOURCE_DIR}/samples/layout/cmaps.cpp"
		)

		if (WIN32)
			target_sources(layout PRIVATE
				"${ICU_SOURCE_DIR}/samples/layout/layout.cpp"
				"${ICU_SOURCE_DIR}/samples/layout/GDIFontInstance.cpp"
				"${ICU_SOURCE_DIR}/samples/layout/GDIFontMap.cpp"
				"${ICU_SOURCE_DIR}/samples/layout/gdiglue.cpp"
				"${ICU_SOURCE_DIR}/samples/layout/GDIGUISupport.cpp"
			)
		else()
			target_sources(layout PRIVATE
				"${ICU_SOURCE_DIR}/samples/layout/gnomelayout.cpp"
				"${ICU_SOURCE_DIR}/samples/layout/GnomeFontInstance.cpp"
				"${ICU_SOURCE_DIR}/samples/layout/GnomeFontMap.cpp"
				"${ICU_SOURCE_DIR}/samples/layout/gnomeglue.cpp"
				"${ICU_SOURCE_DIR}/samples/layout/GnomeGUISupport.cpp"
			)
		endif()

		set(LAYOUT_LINK_FLAGS " ")

		if (MSVC)
			set(LAYOUT_LINK_FLAGS "/SUBSYSTEM:WINDOWS")
		endif()

		target_link_libraries(layout PUBLIC ICU::common ICU::data ICU::layoutex)
		target_include_directories(layout PUBLIC "${ICU_SOURCE_DIR}/samples/layout")
		set_target_properties(layout PROPERTIES
			LINK_FLAGS ${LAYOUT_LINK_FLAGS}
		)

		add_custom_command(TARGET layout POST_BUILD
			COMMAND
			${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ICU::data> $<TARGET_FILE_DIR:layout>
		)
	endif()
endif()
