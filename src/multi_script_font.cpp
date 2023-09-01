#include "multi_script_font.hpp"

#include "font.hpp"
#include "font_cache.hpp"

#include <layout/LEScripts.h>

#include <unicode/utext.h>

static Font* find_compatible_font(FontCache& cache, Font* baseFont, FaceIndex_T fallbackBaseIndex,
		FaceIndex_T fallbackCount);

MultiScriptFont::MultiScriptFont(FontCache& cache, FamilyIndex_T family, FontWeightIndex weight,
			FontFaceStyle style, uint32_t size)
		: m_fontCache(&cache)
		, m_fontKey(static_cast<uint32_t>(style)
				| (static_cast<uint32_t>(weight) << 1)
				| (static_cast<uint32_t>(family) << 16))
		, m_size(size) {}

MultiScriptFont::operator bool() const {
	return m_fontCache;
}

const icu::LEFontInstance* MultiScriptFont::getSubFont(const LEUnicode chars[], le_int32* offset, le_int32 limit,
		le_int32 script, LEErrorCode& success) const {
	if (LE_FAILURE(success)) [[unlikely]] {
        return NULL;
    }

    if (chars == NULL || *offset < 0 || limit < 0 || *offset >= limit || script < 0
			|| script >= icu::scriptCodeCount) [[unlikely]] {
        success = LE_ILLEGAL_ARGUMENT_ERROR;
        return NULL;
    }

	auto* baseFont = m_fontCache->get_font_for_script(get_family(), get_weight(), get_style(),
			static_cast<UScriptCode>(script), m_size);
	auto [fallbackBaseIndex, fallbackCount] = m_fontCache->get_fallback_info(get_family());

	// Find the longest run that the base font or its fallbacks are able to draw
	UText iter UTEXT_INITIALIZER;
	UErrorCode err{};
	utext_openUChars(&iter, chars + *offset, limit - *offset, &err);

	// First, find the first font that is able to render a char from the string.

	Font* targetFont = nullptr;

	for (;;) {
		auto c = UTEXT_NEXT32(&iter);

		if (c == U_SENTINEL) {
			break;
		}
		else if (auto* font = find_compatible_font(c, baseFont, fallbackBaseIndex, fallbackCount)) {
			targetFont = font;
			break;
		}
	}

	// No font can render this substring, just use the base font
	if (!targetFont) {
		*offset = limit;
		return baseFont;
	}
	
	// Then, see how long it is able to render characters

	for (;;) {
		auto idx = UTEXT_GETNATIVEINDEX(&iter);
		auto c = UTEXT_NEXT32(&iter);

		if (c == U_SENTINEL) {
			break;
		}
		else if (targetFont->mapCharToGlyph(c) == 0) {
			*offset = *offset + idx;
			return targetFont;
		}
	}

	*offset = limit;
	return targetFont;
}

const void* MultiScriptFont::getFontTable(LETag tableTag, size_t& length) const {
	return nullptr;
}

le_int32 MultiScriptFont::getUnitsPerEM() const {
	return 1;
}

LEGlyphID MultiScriptFont::mapCharToGlyph(LEUnicode32 ch) const {
	return 0;
}

void MultiScriptFont::getGlyphAdvance(LEGlyphID glyph, LEPoint& advance) const {
	advance.fX = 0.f;
	advance.fY = 0.f;
}

le_bool MultiScriptFont::getGlyphPoint(LEGlyphID /* glyph */, le_int32 /* pointNumber */,
		LEPoint& /* point */) const {
	return false;
}

float MultiScriptFont::getXPixelsPerEm() const {
	return 0.f;
}

float MultiScriptFont::getYPixelsPerEm() const {
	return 0.f;
}

float MultiScriptFont::getScaleFactorX() const {
	return 1.f;
}

float MultiScriptFont::getScaleFactorY() const {
	return 1.f;
}

le_int32 MultiScriptFont::getAscent() const {
	return 0;
}

le_int32 MultiScriptFont::getDescent() const {
	return 0;
}

le_int32 MultiScriptFont::getLeading() const {
	return 0;
}

FontCache* MultiScriptFont::get_font_cache() const {
	return m_fontCache;
}

FamilyIndex_T MultiScriptFont::get_family() const {
	return static_cast<FamilyIndex_T>((m_fontKey >> 16) & 0xFFFFu);
}

FontWeightIndex MultiScriptFont::get_weight() const {
	return static_cast<FontWeightIndex>((m_fontKey >> 1) & 0xFu);
}

FontFaceStyle MultiScriptFont::get_style() const {
	return static_cast<FontFaceStyle>(m_fontKey & 0x1u);
}

uint32_t MultiScriptFont::get_size() const {
	return m_size;
}

Font* MultiScriptFont::find_compatible_font(uint32_t codepoint, Font* baseFont, FaceIndex_T fallbackBaseIndex,
		FaceIndex_T fallbackCount) const {
	if (baseFont && baseFont->mapCharToGlyph(codepoint)) {
		return baseFont;
	}

	for (FaceIndex_T i = 0; i < fallbackCount; ++i) {
		auto* fallback = m_fontCache->get_fallback_font(get_family(), fallbackBaseIndex + i, m_size);

		if (fallback && fallback->mapCharToGlyph(codepoint)) {
			return fallback;
		}
	}

	return nullptr;
}

