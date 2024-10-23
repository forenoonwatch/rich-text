#pragma once

#include "text_alignment.hpp"

namespace Text {

class Font;
class LayoutInfo;
enum class LayoutInfoFlags : uint8_t;
template <typename> class ValueRuns;

/**
 * @brief Builds the paragraph layout using LayoutEx
 */
void build_layout_info_icu_lx(LayoutInfo& result, const char16_t* chars, int32_t count,
		const ValueRuns<Font>& fontRuns, float textAreaWidth, float textAreaHeight,
		YAlignment textYAlignment, LayoutInfoFlags flags);

/**
 * @brief Builds the paragraph layout using direct calls to ubidi.h and usc_impl.h run calculation functions
 */
void build_layout_info_icu(LayoutInfo& result, const char16_t* chars, int32_t count,
		const ValueRuns<Font>& fontRuns, float textAreaWidth, float textAreaHeight,
		YAlignment textYAlignment, LayoutInfoFlags flags);

/**
 * @brief Builds the paragraph layout using UTF-8 APIs
 */
void build_layout_info_utf8(LayoutInfo& result, const char* chars, int32_t count,
		const ValueRuns<Font>& fontRuns, float textAreaWidth, float textAreaHeight,
		YAlignment textYAlignment, LayoutInfoFlags flags, const ValueRuns<bool>* pSmallcapsRuns = nullptr,
		const ValueRuns<bool>* pSubscriptRuns = nullptr, const ValueRuns<bool>* pSuperscriptRuns = nullptr);

/**
 * @brief Converts a UTF-16 LayoutInfo to UTF-8 based indices 
 */
void convert_layout_info_to_utf8(const LayoutInfo& src, LayoutInfo& result, const char16_t* srcChars,
		int32_t srcCharCount, const char* dstChars, int32_t dstCharCount);

}

