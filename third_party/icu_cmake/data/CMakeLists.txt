file(REAL_PATH "../icu/icu4c/source" ICU_SOURCE_DIR BASE_DIRECTORY ${PROJECT_SOURCE_DIR})

set(PKGDATA_LIBSTATICNAME -L icu${DATA_STUBNAME})
set(PKGDATA_LIBNAME -L icu${DATA_STUBNAME})

set(PKGDATA_LIST ${CMAKE_CURRENT_BINARY_DIR}/icudata.lst)

set(ICUDATA_CHAR "l")

set(ICUDATA_BASENAME_VERSION icudt${ICU_VERSION_MAJOR})
set(ICUDATA_ENTRY_POINT icudt${ICU_VERSION_MAJOR})
set(ICUDATA_PLATFORM_NAME ${ICUDATA_BASENAME_VERSION}${ICUDATA_CHAR})
set(ICUDATA_NAME ${ICUDATA_PLATFORM_NAME})

if (MSVC)
  set(ICUDATA_ASM_FILE ${CMAKE_CURRENT_BINARY_DIR}/${ICUDATA_PLATFORM_NAME}_dat.obj)
else()
  set(ICUDATA_ASM_FILE ${CMAKE_CURRENT_BINARY_DIR}/${ICUDATA_PLATFORM_NAME}_dat.S)
endif()

if (MSVC)
	set(PKGDATA_VERSIONING "")
else()
	set(PKGDATA_VERSIONING
		-r ${ICU_VERSION}
	)
endif()

set(GENCCODE_ASSEMBLY "")

if (MINGW)
	if (CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(GENCCODE_ASSEMBLY "-a gcc-mingw64")  # 64 bits
	else()
		set(GENCCODE_ASSEMBLY "-a gcc-cygwin")   # 32 bits
	endif()
elseif (APPLE)
	set(GENCCODE_ASSEMBLY "-a gcc-darwin")
elseif (UNIX)  # for GCC and clang
	set(GENCCODE_ASSEMBLY "-a gcc")
endif()

set(ICUPKG_INC ${CMAKE_CURRENT_BINARY_DIR}/icupkg.inc)

set(ECHO_COMMAND "${CMAKE_COMMAND} -E echo")
if (WIN32)
	set(ECHO_COMMAND "echo")
endif()
configure_file(${CMAKE_CURRENT_LIST_DIR}/icupkg.inc.cmake ${ICUPKG_INC} @ONLY)

set(DATA_EXTRACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/build")
file(MAKE_DIRECTORY ${DATA_EXTRACT_DIR})

set(ICU_DATA_PATH "https://github.com/unicode-org/icu/releases/download/release-73-2/icu4c-73_2-data-bin-l.zip")
set(ICU_DATA_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/icu_data_bin.zip")
set(ICU_DATA_FILE "${DATA_EXTRACT_DIR}/${ICUDATA_PLATFORM_NAME}.dat")

if (NOT EXISTS ${ICU_DATA_ARCHIVE})
	file(DOWNLOAD ${ICU_DATA_PATH} ${ICU_DATA_ARCHIVE})
endif()

if (NOT EXISTS ${ICU_DATA_FILE})
	file(ARCHIVE_EXTRACT INPUT ${ICU_DATA_ARCHIVE} DESTINATION ${DATA_EXTRACT_DIR} PATTERNS "*.dat")
endif()

if (CMAKE_HOST_WIN32 AND NOT (CMAKE_GENERATOR STREQUAL "MSYS Makefiles"))
	# Protection not needed
	set(PKGDATA_EXTRACT_PATTERN *)
else()
	set(PKGDATA_EXTRACT_PATTERN \\*)
endif()

set(PKGDATA_LIST_STAMP ${CMAKE_CURRENT_BINARY_DIR}/pkdata_list_stamp)

add_custom_command(
	OUTPUT ${PKGDATA_LIST} ${PKGDATA_LIST_STAMP}
	COMMAND ${CMAKE_COMMAND} -E remove -f ${PKGDATA_LIST}
	COMMAND icupkg 
		-d ${DATA_EXTRACT_DIR}
		--list
		-x ${PKGDATA_EXTRACT_PATTERN}
		${ICU_DATA_FILE}
		-o ${PKGDATA_LIST}
	COMMAND ${CMAKE_COMMAND} -E touch ${PKGDATA_LIST_STAMP}
	DEPENDS ${ICU_DATA_FILE}
)

if (MSVC)
	set(PKGDATA_OPTS
		-f  # --force-prefix
		-v  # --verbose
	)
else()
	set(PKGDATA_OPTS
		-O ${ICUPKG_INC}  # --bldopt
		-q  # --quiet
	)
endif()

set(PKGDATA
	pkgdata
	${PKGDATA_OPTS}
	-c
	-s ${DATA_EXTRACT_DIR} # source dir
	-d ${CMAKE_CURRENT_BINARY_DIR} # dest dir
)

add_custom_command(
	OUTPUT
		${ICUDATA_ASM_FILE}
	COMMAND ${PKGDATA}
		-e ${ICUDATA_ENTRY_POINT} 			# --entrypoint
		-T ${CMAKE_CURRENT_BINARY_DIR} 		# --tempdir
		-p ${ICUDATA_NAME}         			# --name
		-m dll     							# --mode
		${PKGDATA_VERSIONING}
		${PKGDATA_LIBSTATICNAME}
		${PKGDATA_LIST}
	DEPENDS pkgdata ${PKGDATA_LIST} ${PKGDATA_LIST_STAMP} ${ICU_DATA_FILE}
)

set(lib_NAME ${ICULIBS_DT})
set(lib_NAME_SUFFIX ${DATA_STUBNAME})

add_library(${lib_NAME} SHARED "")

set_target_properties(${lib_NAME} PROPERTIES
	LINKER_LANGUAGE C
	EXPORT_NAME ${lib_NAME_SUFFIX}
	OUTPUT_NAME
		${STATIC_PREFIX}${lib_NAME}${ICULIBSUFFIX_DEBUG}
	RUNTIME_OUTPUT_NAME
		${STATIC_PREFIX}${lib_NAME}${ICULIBSUFFIX_VERSION}${ICULIBSUFFIX_DEBUG}
)

target_include_directories(${lib_NAME} PRIVATE
	${ICU_SOURCE_DIR}/common
)

target_sources(${lib_NAME} PRIVATE
	${ICUDATA_ASM_FILE}
)

add_library(ICU::data ALIAS ${lib_NAME})