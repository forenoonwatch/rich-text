#include "other_layout_builders.hpp"
#include "layout_info.hpp"

#include "font_registry.hpp"
#include "value_runs.hpp"

#include <unicode/utext.h>
#include <unicode/utf16.h>

#include <layout/ParagraphLayout.h>

#include <hb.h>

using namespace Text;

static constexpr const UChar32 CH_LF = 0x000A;
static constexpr const UChar32 CH_CR = 0x000D;
static constexpr const UChar32 CH_LSEP = 0x2028;
static constexpr const UChar32 CH_PSEP = 0x2029;

namespace {

class LESingleScript final : public icu::LEFontInstance {
	public:
		explicit LESingleScript(SingleScriptFont font)
				: m_font(font) {}

		const void* getFontTable(LETag tableTag, size_t &length) const override {
			FontData fontData = FontRegistry::get_font_data(m_font);

			if (auto* blob = hb_face_reference_table(hb_font_get_face(fontData.hbFont), tableTag)) {
				unsigned tmpLength{};
				auto* result = hb_blob_get_data(blob, &tmpLength);
				length = static_cast<size_t>(tmpLength);
				return result;
			}

			return nullptr;
		}

		le_int32 getUnitsPerEM() const override {
			return FontRegistry::get_font_data(m_font).get_upem();
		}

		LEGlyphID mapCharToGlyph(LEUnicode32 ch) const override {
			return FontRegistry::get_font_data(m_font).map_codepoint_to_glyph(ch);
		}

		void getGlyphAdvance(LEGlyphID glyph, LEPoint &advance) const override {
			FontData fontData = FontRegistry::get_font_data(m_font);
			advance.fX = fontData.get_glyph_advance_x(glyph);
			advance.fY = fontData.get_glyph_advance_y(glyph);
		}

		le_bool getGlyphPoint(LEGlyphID /*glyph*/, le_int32 /*pointNumber*/, LEPoint& /*point*/) const override {
			return false;
		}

		float getXPixelsPerEm() const override {
			return FontRegistry::get_font_data(m_font).get_ppem_x();
		}

		float getYPixelsPerEm() const override {
			return FontRegistry::get_font_data(m_font).get_ppem_y();
		}

		float getScaleFactorX() const override {
			return 1.f;
		}

		float getScaleFactorY() const override {
			return 1.f;
		}

		le_int32 getAscent() const override {
			return static_cast<int32_t>(FontRegistry::get_font_data(m_font).get_ascent());
		}

		le_int32 getDescent() const override {
			return static_cast<int32_t>(-FontRegistry::get_font_data(m_font).get_descent());
		}

		le_int32 getLeading() const override {
			return 0.f;
		}

		SingleScriptFont get_font() const {
			return m_font;
		}
	private:
		SingleScriptFont m_font;
};

class LEMultiScript final : public icu::LEFontInstance {
	public:
		explicit LEMultiScript(Font font)
				: m_font(font) {}

		const LEFontInstance* getSubFont(const LEUnicode chars[], le_int32* offset, le_int32 limit,
				le_int32 script, LEErrorCode& success) const override {
			auto subFont = FontRegistry::get_sub_font(m_font, chars, *offset, limit,
					static_cast<UScriptCode>(script));
			m_fontPool.emplace_back(std::make_unique<LESingleScript>(subFont));
			return m_fontPool.back().get();
		}

		const void* getFontTable(LETag, size_t&) const override { return nullptr; }
		le_int32 getUnitsPerEM() const override { return 1; }
		LEGlyphID mapCharToGlyph(LEUnicode32) const override { return 0; }
		void getGlyphAdvance(LEGlyphID, LEPoint&) const override {}
		le_bool getGlyphPoint(LEGlyphID, le_int32, LEPoint&) const override { return false; }
		float getXPixelsPerEm() const override { return 0.f; }
		float getYPixelsPerEm() const override { return 0.f; }
		float getScaleFactorX() const override { return 1.f; }
		float getScaleFactorY() const override { return 1.f; }
		le_int32 getAscent() const override { return 0.f; }
		le_int32 getDescent() const override { return 0.f; }
		le_int32 getLeading() const override { return 0.f; }

	private:
		Font m_font;
		mutable std::vector<std::unique_ptr<LESingleScript>> m_fontPool;
};

}

static void handle_line_icu_lx(LayoutInfo& result, icu::ParagraphLayout::Line& line, int32_t charOffset,
		const char16_t* chars, size_t& highestRun, int32_t& highestRunCharEnd);

// Public Functions

void Text::build_layout_info_icu_lx(LayoutInfo& result, const char16_t* chars, int32_t count,
		const ValueRuns<Font>& fontRuns, float textAreaWidth, float textAreaHeight,
		YAlignment textYAlignment, LayoutInfoFlags flags) {
	result.clear();

	ValueRuns<Font> subsetFontRuns(fontRuns.get_run_count());
	std::vector<LEMultiScript> multiScriptFonts;

	auto* start = chars;
	auto* end = start + count;
	UText iter UTEXT_INITIALIZER;
	UErrorCode err{};
	utext_openUChars(&iter, chars, count, &err);

	int32_t byteIndex = 0;

	UBiDiLevel paragraphLevel = ((flags & LayoutInfoFlags::RIGHT_TO_LEFT) == LayoutInfoFlags::NONE)
			? UBIDI_DEFAULT_LTR : UBIDI_DEFAULT_RTL;

	if ((flags & LayoutInfoFlags::OVERRIDE_DIRECTIONALITY) != LayoutInfoFlags::NONE) {
		paragraphLevel |= UBIDI_LEVEL_OVERRIDE;
	}

	for (;;) {
		auto idx = UTEXT_GETNATIVEINDEX(&iter);
		auto c = UTEXT_NEXT32(&iter);

		if (c == U_SENTINEL || c == CH_LF || c == CH_CR || c == CH_LSEP || c == CH_PSEP) {
			size_t lastHighestRun{};
			int32_t highestRunCharEnd{INT32_MIN};

			if (idx != byteIndex) {
				auto byteCount = idx - byteIndex;

				subsetFontRuns.clear();
				fontRuns.get_runs_subset(byteIndex, byteCount, subsetFontRuns);

				LEErrorCode err{};
				multiScriptFonts.clear();
				multiScriptFonts.reserve(subsetFontRuns.get_run_count());

				for (size_t i = 0; i < subsetFontRuns.get_run_count(); ++i) {
					multiScriptFonts.emplace_back(subsetFontRuns.get_run_value(i));
				}

				icu::FontRuns icuFontRuns(subsetFontRuns.get_run_count());

				for (size_t i = 0; i < subsetFontRuns.get_run_count(); ++i) {
					icuFontRuns.add(&multiScriptFonts[i], subsetFontRuns.get_run_limit(i));
				}

				icu::ParagraphLayout pl(chars + byteIndex, byteCount, &icuFontRuns, nullptr,
						nullptr, nullptr, paragraphLevel, false, err);

				if (paragraphLevel == UBIDI_DEFAULT_LTR) {
					paragraphLevel = pl.getParagraphLevel();
				}

				auto firstParagraphRun = result.get_run_count();

				while (auto* pLine = pl.nextLine(textAreaWidth)) {
					handle_line_icu_lx(result, *pLine, byteIndex, chars, lastHighestRun, highestRunCharEnd);
					delete pLine;
				}
			}
			else {
				auto font = fontRuns.get_value(byteIndex == count ? count - 1 : byteIndex);
				auto fontData = FontRegistry::get_font_data(font);
				auto height = fontData.get_ascent() - fontData.get_descent();

				lastHighestRun = result.get_run_count();
				highestRunCharEnd = byteIndex;
				result.append_empty_line(FontRegistry::get_default_single_script_font(font),
						static_cast<uint32_t>(byteIndex), height, fontData.get_ascent());
			}

			if (c == U_SENTINEL) {
				break;
			}
			else if (c == CH_CR && UTEXT_CURRENT32(&iter) == CH_LF) {
				UTEXT_NEXT32(&iter);
			}

			byteIndex = UTEXT_GETNATIVEINDEX(&iter);

			result.set_run_char_end_offset(lastHighestRun, byteIndex - idx);
		}
	}

	auto totalHeight = result.get_text_height();
	result.set_text_start_y(static_cast<float>(textYAlignment) * (textAreaHeight - totalHeight) * 0.5f);
}

static void handle_line_icu_lx(LayoutInfo& result, icu::ParagraphLayout::Line& line,
		int32_t charOffset, const char16_t* chars, size_t& highestRun, int32_t& highestRunCharEnd) {
	result.reserve_runs(result.get_run_count() + line.countRuns());

	int32_t maxAscent = 0;
	int32_t maxDescent = 0;

	for (int32_t i = 0; i < line.countRuns(); ++i) {
		auto* pRun = line.getVisualRun(i);
		auto ascent = pRun->getAscent();
		auto descent = pRun->getDescent();
		auto* pGlyphs = pRun->getGlyphs();
		auto* pGlyphPositions = pRun->getPositions();
		auto* pCharMap = pRun->getGlyphToCharMap();
		auto glyphCount = pRun->getGlyphCount();
		bool rightToLeft = pRun->getDirection() == UBIDI_RTL;

		if (ascent > maxAscent) {
			maxAscent = ascent;
		}

		if (descent > maxDescent) {
			maxDescent = descent;
		}

		for (int32_t j = 0; j < glyphCount; ++j) {
			if (pGlyphs[j] != 0xFFFF) {
				result.append_glyph(pGlyphs[j]);
				result.append_char_index(pCharMap[j] + charOffset);
				result.append_glyph_position(pGlyphPositions[2 * j], pGlyphPositions[2 * j + 1]);
			}
		}

		result.append_glyph_position(pGlyphPositions[2 * glyphCount], pGlyphPositions[2 * glyphCount + 1]);

		auto firstChar = pCharMap[0] + charOffset;
		auto lastChar = pCharMap[glyphCount - 1] + charOffset;

		if (rightToLeft) {
			std::swap(firstChar, lastChar);
		}

		U16_FWD_1_UNSAFE(chars, lastChar);

		if (lastChar > highestRunCharEnd) {
			highestRun = result.get_run_count();
			highestRunCharEnd = lastChar;
		}

		auto* pLEFont = static_cast<const LESingleScript*>(pRun->getFont());
		result.append_run(pLEFont->get_font(), static_cast<uint32_t>(firstChar),
				static_cast<uint32_t>(lastChar), rightToLeft);
	}

	result.append_line(static_cast<float>(maxAscent + maxDescent), static_cast<float>(maxAscent), false);
}

