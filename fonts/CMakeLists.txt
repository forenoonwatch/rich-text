set(NOTO_SANS_MAIN_URL "https://github.com/notofonts/latin-greek-cyrillic/releases/download/NotoSans-v2.012/NotoSans-v2.012.zip")
set(NOTO_SANS_JP_URL "https://github.com/notofonts/noto-cjk/releases/download/Sans2.004/06_NotoSansCJKjp.zip")

set(ARCHIVE_TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/tmp")
file(MAKE_DIRECTORY ${ARCHIVE_TEMP_DIR})

file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/families" "${CMAKE_CURRENT_BINARY_DIR}/families" COPY_ON_ERROR SYMBOLIC)

file(MAKE_DIRECTORY ${ARCHIVE_TEMP_DIR})

function(fetch_font FONT_NAME DOWNLOAD_URL PATH_FILTER)
	get_filename_component(ARCHIVE_FILE_NAME ${DOWNLOAD_URL} NAME)
	set(ARCHIVE_FULL_FILE_PATH "${ARCHIVE_TEMP_DIR}/${ARCHIVE_FILE_NAME}")

	if (NOT EXISTS ${ARCHIVE_FULL_FILE_PATH})
		file(DOWNLOAD ${DOWNLOAD_URL} ${ARCHIVE_FULL_FILE_PATH})

		file(ARCHIVE_EXTRACT
			INPUT ${ARCHIVE_FULL_FILE_PATH}
			DESTINATION ${ARCHIVE_TEMP_DIR}
			PATTERNS
				${PATH_FILTER}
		)

		set(FONT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${FONT_NAME}")

		file(GLOB_RECURSE FONT_EXTRACTED_FILES
			"${ARCHIVE_TEMP_DIR}/*.ttf"
			"${ARCHIVE_TEMP_DIR}/*.otf"
		)

		file(MAKE_DIRECTORY ${FONT_DIRECTORY})

		foreach (FONT_FULL_FILE_PATH ${FONT_EXTRACTED_FILES})
			get_filename_component(FONT_FILE_NAME ${FONT_FULL_FILE_PATH} NAME)
			file(RENAME ${FONT_FULL_FILE_PATH} "${FONT_DIRECTORY}/${FONT_FILE_NAME}")
		endforeach()
	endif()
endfunction()

fetch_font("NotoSans" ${NOTO_SANS_MAIN_URL} "*NotoSans/full/ttf/*.ttf")
fetch_font("NotoSans" ${NOTO_SANS_JP_URL} "*.otf")