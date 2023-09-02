cmake_minimum_required(VERSION 3.18)

option(ICU_BUILD_TESTS "Build ICU tests" OFF)

option(ICU_CHARSET_IS_UTF8 "Whether the default charset is UTF8, otherwise it is UTF16" OFF)

file(REAL_PATH "../icu/icu4c/source" ICU_SOURCE_DIR BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

include(${CMAKE_CURRENT_LIST_DIR}/ICUHelpers.cmake)

get_icu_version(ICU_VERSION)
get_version_parts(${ICU_VERSION} ICU_VERSION_MAJOR ICU_VERSION_MINOR ICU_VERSION_PATCH ICU_VERSION_TWEAK)

project(
	ICU VERSION ${ICU_VERSION}
	LANGUAGES C CXX ASM
)

set(COMMON_STUBNAME uc)
set(DATA_STUBNAME data)
set(I18N_STUBNAME i18n)
set(TOOLUTIL_STUBNAME tu)
set(LAYOUTEX_STUBNAME lx)
set(CTESTFW_STUBNAME test)

if (WIN32)
  set(DATA_STUBNAME dt)
  set(I18N_STUBNAME in)
endif()

if (MSVC)
	if (NOT BUILD_SHARED_LIBS)
		# Static library prefix
		set(STATIC_PREFIX "s")
	endif()
	
	set(ICULIBSUFFIX_VERSION ${PROJECT_VERSION_MAJOR})
	set(ICULIBSUFFIX_DEBUG $<$<CONFIG:Debug>:d>)
endif()

set(ICULIBS_DT icu${DATA_STUBNAME})
set(ICULIBS_UC icu${COMMON_STUBNAME})
set(ICULIBS_I18N icu${I18N_STUBNAME})
set(ICULIBS_TOOLUTIL icu${TOOLUTIL_STUBNAME})
set(ICULIBS_LX icu${LAYOUTEX_STUBNAME})
set(ICULIBS_CTESTFW icu${CTESTFW_STUBNAME})

add_subdirectory(common)
add_subdirectory(i18n)
add_subdirectory(stubdata)
add_subdirectory(tools)
add_subdirectory(data)
add_subdirectory(layoutex)
add_subdirectory(samples)
