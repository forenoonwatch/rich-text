#pragma once

#include "font.hpp"
#include "text_alignment.hpp"

#include <unicode/uversion.h>

#include <vector>

U_NAMESPACE_BEGIN

class BreakIterator;
class Locale;

U_NAMESPACE_END

struct hb_buffer_t;
struct _SBParagraph;

namespace Text {

class LayoutInfo;
enum class LayoutInfoFlags : uint8_t;
template <typename> class ValueRuns;
template <typename> class ValueRunsIterator;
template <typename> class MaybeDefaultRunsIterator;

struct LayoutBuildParams {
	float textAreaWidth;
	float textAreaHeight;
	float tabWidth;
	LayoutInfoFlags flags;
	XAlignment xAlignment;
	YAlignment yAlignment;
	const ValueRuns<bool>* pSmallcapsRuns;
	const ValueRuns<bool>* pSubscriptRuns;
	const ValueRuns<bool>* pSuperscriptRuns;
};

class LayoutBuilder {
	public:
		explicit LayoutBuilder();
		~LayoutBuilder();

		LayoutBuilder(LayoutBuilder&&) noexcept;
		LayoutBuilder& operator=(LayoutBuilder&&) noexcept;

		LayoutBuilder(const LayoutBuilder&) = delete;
		void operator=(const LayoutBuilder&) = delete;

		void build_layout_info(LayoutInfo&, const char* chars, int32_t count, const ValueRuns<Font>& fontRuns,
				const LayoutBuildParams& params);
	private:
		struct LogicalRun {
			SingleScriptFont font;
			int32_t charEndIndex;
			uint32_t glyphEndIndex;
		};

		icu::BreakIterator* m_lineBreakIterator{};
		hb_buffer_t* m_buffer{};
		std::vector<uint32_t> m_glyphs;
		std::vector<uint32_t> m_charIndices;
		// Glyph positions are stored as 26.6 fixed point values, always in logical order. On the primary axis,
		// positions are stored as glyph widths. On the secondary axis, positions are stored in absolute
		// position as calculated from the offsets and advances.
		std::vector<int32_t> m_glyphPositions[2];

		int32_t m_cursorX;
		int32_t m_cursorY;

		std::vector<LogicalRun> m_logicalRuns;

		size_t build_paragraph(LayoutInfo& result, _SBParagraph* sbParagraph, const char* fullText,
				int32_t paragraphLength, int32_t paragraphStart, ValueRunsIterator<Font>& itFont,
				MaybeDefaultRunsIterator<bool>& itSmallcaps, MaybeDefaultRunsIterator<bool>& itSubscript,
				MaybeDefaultRunsIterator<bool>& itSuperscript, int32_t textAreaWidthFixed,
				int32_t tabWidthFixed, const icu::Locale& defaultLocale, bool tabWidthFromPixels);
		void shape_logical_run(const SingleScriptFont& font, const char* paragraphText, int32_t offset,
				int32_t count, int32_t paragraphStart, int32_t paragraphLength, int script,
				const icu::Locale& locale, bool rightToLeft);
		void compute_line_visual_runs(LayoutInfo& result, _SBParagraph* sbParagraph, const char* chars,
				int32_t count, int32_t lineStart, int32_t lineEnd, size_t& highestRun,
				int32_t& highestRunCharEnd);
		void append_visual_run(LayoutInfo& result, size_t logicalRunIndex, int32_t charStartIndex,
				int32_t charEndIndex, int32_t& visualRunLastX, size_t& highestRun, int32_t& highestRunCharEnd,
				bool rightToLeft);

		void apply_tab_widths_no_line_break(const char* fullText, int32_t tabWidthFixed,
				bool tabWidthFromPixels);

		void reset(size_t capacity);
};

}

