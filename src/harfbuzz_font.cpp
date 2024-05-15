/*
 * Copyright © 2009  Red Hat, Inc.
 * Copyright © 2009  Keith Stribley
 * Copyright © 2015  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Red Hat Author(s): Behdad Esfahbod
 * Google Author(s): Behdad Esfahbod
 */

#include "harfbuzz_font.hpp"

#include "hb-cache.hh"
#include "hb-font.hh"
#include "hb-machinery.hh"
#include "hb-ot-os2-table.hh"
#include "hb-ot-shaper-arabic-pua.hh"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_TRUETYPE_TABLES_H

/* TODO:
 *
 * In general, this file does a fine job of what it's supposed to do.
 * There are, however, things that need more work:
 *
 *   - FreeType works in 26.6 mode.  Clients can decide to use that mode, and everything
 *	 would work fine.  However, we also abuse this API for performing in font-space,
 *	 but don't pass the correct flags to FreeType.  We just abuse the no-hinting mode
 *	 for that, such that no rounding etc happens.  As such, we don't set ppem, and
 *	 pass NO_HINTING as load_flags.  Would be much better to use NO_SCALE, and scale
 *	 ourselves.
 *
 *   - We don't handle / allow for emboldening / obliqueing.
 *
 *   - In the future, we should add constructors to create fonts in font space?
 */

static constexpr const int FREETYPE_LOAD_FLAGS = FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP;

using hb_ft_advance_cache_t = hb_cache_t<16, 24, 8, false>;

namespace {

struct HarfbuzzFontImpl {
	FT_Face ftFace;
	unsigned cachedSerial{static_cast<unsigned>(-1)};
	mutable hb_ft_advance_cache_t advanceCache;
	bool isSymbolCharmap: 1;
	bool applyFTFaceTransform: 1;
};

struct FontFuncsLazyLoader : hb_font_funcs_lazy_loader_t<FontFuncsLazyLoader> {
	static hb_font_funcs_t* create();
	static void at_exit();
} g_fontFuncsLoader;

}

static hb_face_t* harfbuzz_face_create(FT_Face ftFace);

static void harfbuzz_font_destroy(void* data);

static void set_font_funcs(hb_font_t* font, FT_Face ftFace);

static hb_blob_t* face_reference_table(hb_face_t* face, hb_tag_t tag, void* userData);

static hb_bool_t hb_ft_get_nominal_glyph(hb_font_t* font, void* fontData, hb_codepoint_t unicode,
		hb_codepoint_t* pGlyph, void* userData);
static unsigned hb_ft_get_nominal_glyphs(hb_font_t* font, void* fontData, unsigned count,
		const hb_codepoint_t* pFirstUnicode, unsigned unicodeStride, hb_codepoint_t* pFirstGlyph,
		unsigned glyphStride, void* userData);
static hb_bool_t hb_ft_get_variation_glyph(hb_font_t* font, void* fontData, hb_codepoint_t unicode,
		hb_codepoint_t variationSelector, hb_codepoint_t* pGlyph, void* userData);

static hb_bool_t hb_ft_get_font_h_extents(hb_font_t* font, void* fontData, hb_font_extents_t* pMetrics,
		void* userData);
static void hb_ft_get_glyph_h_advances(hb_font_t* font, void* fontData, unsigned count,
		const hb_codepoint_t* pFirstGlyph, unsigned glyphStride, hb_position_t* pFirstAdvance,
		unsigned advanceStride, void* userData);

static hb_position_t hb_ft_get_glyph_v_advance(hb_font_t* font, void* fontData, hb_codepoint_t glyph,
		void* userData);
static hb_bool_t hb_ft_get_glyph_v_origin(hb_font_t* font, void* fontData, hb_codepoint_t glyph,
		hb_position_t* pX, hb_position_t* pY, void* userData);

static hb_position_t hb_ft_get_glyph_h_kerning(hb_font_t* font, void* fontData, hb_codepoint_t leftGlyph,
		hb_codepoint_t rightGlyph, void* userData);

static hb_bool_t hb_ft_get_glyph_extents(hb_font_t* font, void* fontData, hb_codepoint_t glyph,
		hb_glyph_extents_t* pExtents, void* userData);
static hb_bool_t hb_ft_get_glyph_contour_point(hb_font_t* font, void* fontData, hb_codepoint_t glyph,
		unsigned pointIndex, hb_position_t* pX, hb_position_t* pY, void* userData);
static hb_bool_t hb_ft_get_glyph_name(hb_font_t* font, void* fontData, hb_codepoint_t glyph, char* name,
		unsigned size, void* userData);
static hb_bool_t hb_ft_get_glyph_from_name(hb_font_t* font, void* fontData, const char* name, int len,
		hb_codepoint_t* pGlyph, void* userData);

// Public Functions

hb_font_t* Text::harfbuzz_font_create(FT_Face ftFace) {
	auto* face = harfbuzz_face_create(ftFace);
	auto* font = hb_font_create(face);

	set_font_funcs(font, ftFace);
	harfbuzz_font_mark_changed(font);

	return font;
}

void Text::harfbuzz_font_mark_changed(hb_font_t* font) {
	auto* pImpl = reinterpret_cast<HarfbuzzFontImpl*>(font->user_data);
	FT_Face ftFace = pImpl->ftFace;

	int scaleX = (int)(((uint64_t)ftFace->size->metrics.x_scale * (uint64_t)ftFace->units_per_EM
			+ (1u << 15)) >> 16);
	int scaleY = (int)(((uint64_t)ftFace->size->metrics.y_scale * (uint64_t)ftFace->units_per_EM
			+ (1u << 15)) >> 16);

	hb_font_set_scale(font, scaleX, scaleY);

	pImpl->advanceCache.clear();
	pImpl->cachedSerial = font->serial;
}

// FontFuncsLazyLoader

hb_font_funcs_t* FontFuncsLazyLoader::create() {
	auto* funcs = hb_font_funcs_create();

	hb_font_funcs_set_nominal_glyph_func(funcs, hb_ft_get_nominal_glyph, nullptr, nullptr);
	hb_font_funcs_set_nominal_glyphs_func(funcs, hb_ft_get_nominal_glyphs, nullptr, nullptr);
	hb_font_funcs_set_variation_glyph_func(funcs, hb_ft_get_variation_glyph, nullptr, nullptr);

	hb_font_funcs_set_font_h_extents_func(funcs, hb_ft_get_font_h_extents, nullptr, nullptr);
	hb_font_funcs_set_glyph_h_advances_func(funcs, hb_ft_get_glyph_h_advances, nullptr, nullptr);
	//hb_font_funcs_set_glyph_h_origin_func(funcs, hb_ft_get_glyph_h_origin, nullptr, nullptr);

#ifndef HB_NO_VERTICAL
	//hb_font_funcs_set_font_v_extents_func(funcs, hb_ft_get_font_v_extents, nullptr, nullptr);
	hb_font_funcs_set_glyph_v_advance_func(funcs, hb_ft_get_glyph_v_advance, nullptr, nullptr);
	hb_font_funcs_set_glyph_v_origin_func(funcs, hb_ft_get_glyph_v_origin, nullptr, nullptr);
#endif

#ifndef HB_NO_OT_SHAPE_FALLBACK
	hb_font_funcs_set_glyph_h_kerning_func(funcs, hb_ft_get_glyph_h_kerning, nullptr, nullptr);
#endif
	//hb_font_funcs_set_glyph_v_kerning_func(funcs, hb_ft_get_glyph_v_kerning, nullptr, nullptr);
	hb_font_funcs_set_glyph_extents_func(funcs, hb_ft_get_glyph_extents, nullptr, nullptr);
	hb_font_funcs_set_glyph_contour_point_func(funcs, hb_ft_get_glyph_contour_point, nullptr, nullptr);
	hb_font_funcs_set_glyph_name_func(funcs, hb_ft_get_glyph_name, nullptr, nullptr);
	hb_font_funcs_set_glyph_from_name_func(funcs, hb_ft_get_glyph_from_name, nullptr, nullptr);

	hb_font_funcs_make_immutable(funcs);
	hb_atexit(FontFuncsLazyLoader::at_exit);

	return funcs;
}

void FontFuncsLazyLoader::at_exit() {
	g_fontFuncsLoader.free_instance();
}

// Static Functions

static hb_face_t* harfbuzz_face_create(FT_Face ftFace) {
	hb_face_t* face;

	if (!ftFace->stream->read) {
		auto* blob = hb_blob_create((const char*)ftFace->stream->base, (unsigned)ftFace->stream->size,
				HB_MEMORY_MODE_READONLY, ftFace, nullptr);
		face = hb_face_create(blob, ftFace->face_index);
		hb_blob_destroy(blob);
	}
	else {
		face = hb_face_create_for_tables(face_reference_table, ftFace, nullptr);
	}

	hb_face_set_index(face, ftFace->face_index);
	hb_face_set_upem(face, ftFace->units_per_EM);

	return face;
}

static void harfbuzz_font_destroy(void* data) {
	auto* pImpl = reinterpret_cast<HarfbuzzFontImpl*>(data);
	delete pImpl;
}

static void set_font_funcs(hb_font_t* font, FT_Face ftFace) {
	bool isSymbolCharmap = ftFace->charmap && ftFace->charmap->encoding == FT_ENCODING_MS_SYMBOL;

	auto* pImpl = new HarfbuzzFontImpl{
		.ftFace = ftFace,
		.isSymbolCharmap = isSymbolCharmap,
		.applyFTFaceTransform = false,
	};

	hb_font_set_funcs(font, g_fontFuncsLoader.get_unconst(), pImpl, harfbuzz_font_destroy);
}

static hb_blob_t* face_reference_table(hb_face_t* /*face*/, hb_tag_t tag, void* userData) {
	auto* ftFace = reinterpret_cast<FT_Face>(userData);
	FT_ULong  length = 0;
	FT_Error error;

	/* Note: FreeType like HarfBuzz uses the NONE tag for fetching the entire blob */

	error = FT_Load_Sfnt_Table(ftFace, tag, 0, nullptr, &length);
	if (error) {
		return nullptr;
	}

	auto* buffer = reinterpret_cast<FT_Byte*>(hb_malloc(length));
	if (!buffer) {
		return nullptr;
	}

	error = FT_Load_Sfnt_Table(ftFace, tag, 0, buffer, &length);
	if (error) {
		hb_free (buffer);
		return nullptr;
	}

	return hb_blob_create(reinterpret_cast<const char*>(buffer), length, HB_MEMORY_MODE_WRITABLE, buffer,
			hb_free);
}

static hb_bool_t hb_ft_get_nominal_glyph(hb_font_t* font, void *fontData, hb_codepoint_t unicode,
		hb_codepoint_t* pGlyph, void* /*userData*/) {
  auto* pImpl = reinterpret_cast<const HarfbuzzFontImpl*>(fontData);
  unsigned g = FT_Get_Char_Index(pImpl->ftFace, unicode);

	if (!g) [[unlikely]] {
		if (pImpl->isSymbolCharmap) [[unlikely]] {
			switch ((unsigned)font->face->table.OS2->get_font_page()) {
				case OT::OS2::font_page_t::FONT_PAGE_NONE:
					if (unicode <= 0x00FFu) {
						/* For symbol-encoded OpenType fonts, we duplicate the
						* U+F000..F0FF range at U+0000..U+00FF.  That's what
						* Windows seems to do, and that's hinted about at:
						* https://docs.microsoft.com/en-us/typography/opentype/spec/recom
						* under "Non-Standard (Symbol) Fonts". */
						g = FT_Get_Char_Index(pImpl->ftFace, 0xF000u + unicode);
					}
				break;
#ifndef HB_NO_OT_SHAPER_ARABIC_FALLBACK
				case OT::OS2::font_page_t::FONT_PAGE_SIMP_ARABIC:
					g = FT_Get_Char_Index (pImpl->ftFace, _hb_arabic_pua_simp_map(unicode));
					break;
				case OT::OS2::font_page_t::FONT_PAGE_TRAD_ARABIC:
					g = FT_Get_Char_Index (pImpl->ftFace, _hb_arabic_pua_trad_map(unicode));
					break;
#endif
				default:
					break;
			}

			if (!g) {
				return false;
			}
		}
		else {
			return false;
		}
	}

	*pGlyph = g;
	return true;
}

static unsigned hb_ft_get_nominal_glyphs(hb_font_t* /*font*/, void* fontData, unsigned count,
		const hb_codepoint_t* pFirstUnicode, unsigned unicodeStride, hb_codepoint_t* pFirstGlyph,
		unsigned glyphStride, void* /*userData*/) {
	auto* pImpl = reinterpret_cast<const HarfbuzzFontImpl*>(fontData);
	unsigned done;

	for (done = 0; done < count && (*pFirstGlyph = FT_Get_Char_Index(pImpl->ftFace, *pFirstUnicode)); ++done) {
		pFirstUnicode = &StructAtOffsetUnaligned<hb_codepoint_t>(pFirstUnicode, unicodeStride);
		pFirstGlyph = &StructAtOffsetUnaligned<hb_codepoint_t>(pFirstGlyph, glyphStride);
	}

	/* We don't need to do the pImpl->isSymbolCharmap dance here, since HB calls the singular nominal_glyph()
	 * for what we don't handle here. */
	return done;
}

static hb_bool_t hb_ft_get_variation_glyph(hb_font_t* font, void* fontData, hb_codepoint_t unicode,
		hb_codepoint_t variationSelector, hb_codepoint_t* pGlyph, void* /*userData*/) {
	auto* pImpl = reinterpret_cast<const HarfbuzzFontImpl*>(fontData);

	if (unsigned g = FT_Face_GetCharVariantIndex(pImpl->ftFace, unicode, variationSelector)) [[likely]] {
		*pGlyph = g;
		return true;
	}

	return false;
}

static void hb_ft_get_glyph_h_advances(hb_font_t* font, void* fontData, unsigned count,
		const hb_codepoint_t* pFirstGlyph, unsigned glyphStride, hb_position_t* pFirstAdvance,
		unsigned advanceStride, void* /*userData*/) {
	auto* pImpl = reinterpret_cast<const HarfbuzzFontImpl*>(fontData);
	auto* origFirstAdvance = pFirstAdvance;
	FT_Face ftFace = pImpl->ftFace;

	float x_mult;
#ifdef HAVE_FT_GET_TRANSFORM
  if (ft_font->transform)
  {
	FT_Matrix matrix;
	FT_Get_Transform (ft_face, &matrix, nullptr);
	x_mult = sqrtf ((float)matrix.xx * matrix.xx + (float)matrix.xy * matrix.xy) / 65536.f;
	x_mult *= font->x_scale < 0 ? -1 : +1;
  }
  else
#endif
  {
	x_mult = font->x_scale < 0 ? -1 : +1;
  }

	for (unsigned i = 0; i < count; i++) {
		FT_Fixed v = 0;
		hb_codepoint_t glyph = *pFirstGlyph;

		if (unsigned cv; pImpl->advanceCache.get(glyph, &cv)) {
		  v = cv;
		}
		else {
			FT_Get_Advance(ftFace, glyph, FREETYPE_LOAD_FLAGS, &v);
			/* Work around bug that FreeType seems to return negative advance
			* for variable-set fonts if x_scale is negative! */
			v = abs (v);
			v = (int) (v * x_mult + (1<<9)) >> 10;
			pImpl->advanceCache.set(glyph, v);
		}

		*pFirstAdvance = v;
		pFirstGlyph = &StructAtOffsetUnaligned<hb_codepoint_t>(pFirstGlyph, glyphStride);
		pFirstAdvance = &StructAtOffsetUnaligned<hb_position_t>(pFirstAdvance, advanceStride);
	}

	// FIXME: This is where I should apply emboldening advance scale, and other synthetic advance scales
	if (font->x_strength && !font->embolden_in_place) {
		/* Emboldening. */
		hb_position_t x_strength = font->x_scale >= 0 ? font->x_strength : -font->x_strength;
		pFirstAdvance = origFirstAdvance;

		for (unsigned i = 0; i < count; i++) {
			*pFirstAdvance += *pFirstAdvance ? x_strength : 0;
			pFirstAdvance = &StructAtOffsetUnaligned<hb_position_t>(pFirstAdvance, advanceStride);
		}
	}
}

static hb_position_t hb_ft_get_glyph_v_advance(hb_font_t* font, void* fontData, hb_codepoint_t glyph,
		void* /*userData*/) {
	auto* pImpl = reinterpret_cast<const HarfbuzzFontImpl*>(fontData);
	FT_Fixed v;
	float y_mult;

#ifdef HAVE_FT_GET_TRANSFORM
  if (ft_font->transform)
  {
	FT_Matrix matrix;
	FT_Get_Transform (ft_font->ft_face, &matrix, nullptr);
	y_mult = sqrtf ((float)matrix.yx * matrix.yx + (float)matrix.yy * matrix.yy) / 65536.f;
	y_mult *= font->y_scale < 0 ? -1 : +1;
  }
  else
#endif
  {
	y_mult = font->y_scale < 0 ? -1 : +1;
  }

	if (FT_Get_Advance(pImpl->ftFace, glyph, FREETYPE_LOAD_FLAGS | FT_LOAD_VERTICAL_LAYOUT, &v)) [[unlikely]] {
		return 0;
	}

	v = (int)(y_mult * v);

	/* Note: FreeType's vertical metrics grows downward while other FreeType coordinates
	* have a Y growing upward.  Hence the extra negation. */

	hb_position_t y_strength = font->y_scale >= 0 ? font->y_strength : -font->y_strength;
	return ((-v + (1<<9)) >> 10) + (font->embolden_in_place ? 0 : y_strength);
}

static hb_bool_t hb_ft_get_glyph_v_origin(hb_font_t* font, void* fontData, hb_codepoint_t glyph,
		hb_position_t* pX, hb_position_t* pY, void* /*userData*/) {
	auto* pImpl = reinterpret_cast<const HarfbuzzFontImpl*>(fontData);
	FT_Face ftFace = pImpl->ftFace;
	float x_mult, y_mult;

#ifdef HAVE_FT_GET_TRANSFORM
  if (ft_font->transform)
  {
	FT_Matrix matrix;
	FT_Get_Transform (ft_face, &matrix, nullptr);
	x_mult = sqrtf ((float)matrix.xx * matrix.xx + (float)matrix.xy * matrix.xy) / 65536.f;
	x_mult *= font->x_scale < 0 ? -1 : +1;
	y_mult = sqrtf ((float)matrix.yx * matrix.yx + (float)matrix.yy * matrix.yy) / 65536.f;
	y_mult *= font->y_scale < 0 ? -1 : +1;
  }
  else
#endif
  {
	x_mult = font->x_scale < 0 ? -1 : +1;
	y_mult = font->y_scale < 0 ? -1 : +1;
  }

	if (FT_Load_Glyph(ftFace, glyph, FREETYPE_LOAD_FLAGS)) [[unlikely]] {
		return false;
	}

	/* Note: FreeType's vertical metrics grows downward while other FreeType coordinates
	* have a Y growing upward.  Hence the extra negation. */
	*pX = ftFace->glyph->metrics.horiBearingX -   ftFace->glyph->metrics.vertBearingX;
	*pY = ftFace->glyph->metrics.horiBearingY - (-ftFace->glyph->metrics.vertBearingY);

	*pX = (hb_position_t)(x_mult * *pX);
	*pY = (hb_position_t)(y_mult * *pY);

	return true;
}

#ifndef HB_NO_OT_SHAPE_FALLBACK
static hb_position_t hb_ft_get_glyph_h_kerning(hb_font_t* font, void* fontData, hb_codepoint_t leftGlyph,
		hb_codepoint_t rightGlyph, void* /*userData*/) {
	auto* pImpl = reinterpret_cast<const HarfbuzzFontImpl*>(fontData);
	FT_Vector kerningv;

	FT_Kerning_Mode mode = font->x_ppem ? FT_KERNING_DEFAULT : FT_KERNING_UNFITTED;
	if (FT_Get_Kerning(pImpl->ftFace, leftGlyph, rightGlyph, mode, &kerningv)) {
		return 0;
	}

	return kerningv.x;
}
#endif

static hb_bool_t hb_ft_get_glyph_extents (hb_font_t* font, void* fontData, hb_codepoint_t glyph,
		hb_glyph_extents_t* pExtents, void* /*userData*/) {
	auto* pImpl = reinterpret_cast<const HarfbuzzFontImpl*>(fontData);
	FT_Face ftFace = pImpl->ftFace;
	float x_mult, y_mult;
	float slant_xy = font->slant_xy;

#ifdef HAVE_FT_GET_TRANSFORM
  if (ft_font->transform)
  {
	FT_Matrix matrix;
	FT_Get_Transform (ft_face, &matrix, nullptr);
	x_mult = sqrtf ((float)matrix.xx * matrix.xx + (float)matrix.xy * matrix.xy) / 65536.f;
	x_mult *= font->x_scale < 0 ? -1 : +1;
	y_mult = sqrtf ((float)matrix.yx * matrix.yx + (float)matrix.yy * matrix.yy) / 65536.f;
	y_mult *= font->y_scale < 0 ? -1 : +1;
  }
  else
#endif
  {
	x_mult = font->x_scale < 0 ? -1 : +1;
	y_mult = font->y_scale < 0 ? -1 : +1;
  }

	if (FT_Load_Glyph(ftFace, glyph, FREETYPE_LOAD_FLAGS)) [[unlikely]] {
		return false;
	}

	/* Copied from hb_font_t::scale_glyph_extents. */

	float x1 = x_mult * ftFace->glyph->metrics.horiBearingX;
	float y1 = y_mult * ftFace->glyph->metrics.horiBearingY;
	float x2 = x1 + x_mult *  ftFace->glyph->metrics.width;
	float y2 = y1 + y_mult * -ftFace->glyph->metrics.height;

	/* Apply slant. */
	if (slant_xy) {
		x1 += hb_min (y1 * slant_xy, y2 * slant_xy);
		x2 += hb_max (y1 * slant_xy, y2 * slant_xy);
	}

	pExtents->x_bearing = floorf(x1);
	pExtents->y_bearing = floorf(y1);
	pExtents->width = ceilf(x2) - pExtents->x_bearing;
	pExtents->height = ceilf(y2) - pExtents->y_bearing;

	if (font->x_strength || font->y_strength) {
		/* Y */
		int y_shift = font->y_strength;

		if (font->y_scale < 0) {
			y_shift = -y_shift;
		}

		pExtents->y_bearing += y_shift;
		pExtents->height -= y_shift;

		/* X */
		int x_shift = font->x_strength;

		if (font->x_scale < 0) {
			x_shift = -x_shift;
		}

		if (font->embolden_in_place) {
			pExtents->x_bearing -= x_shift / 2;
		}

		pExtents->width += x_shift;
	}

	return true;
}

static hb_bool_t hb_ft_get_glyph_contour_point(hb_font_t* /*font*/, void* fontData, hb_codepoint_t glyph,
		unsigned pointIndex, hb_position_t* pX, hb_position_t* pY, void* /*userData*/) {
	auto* pImpl = reinterpret_cast<const HarfbuzzFontImpl*>(fontData);
	FT_Face ftFace = pImpl->ftFace;

	if (FT_Load_Glyph(ftFace, glyph, FREETYPE_LOAD_FLAGS)) [[unlikely]] {
		return false;
	}

	if (ftFace->glyph->format != FT_GLYPH_FORMAT_OUTLINE) [[unlikely]] {
		return false;
	}

	if (pointIndex >= (unsigned)ftFace->glyph->outline.n_points) [[unlikely]] {
		return false;
	}

	*pX = ftFace->glyph->outline.points[pointIndex].x;
	*pY = ftFace->glyph->outline.points[pointIndex].y;

	return true;
}

static hb_bool_t hb_ft_get_glyph_name(hb_font_t* /*font*/, void* fontData, hb_codepoint_t glyph,
		char* name, unsigned size, void* /*userData*/) {
	auto* pImpl = reinterpret_cast<const HarfbuzzFontImpl*>(fontData);
	FT_Face ftFace = pImpl->ftFace;

	hb_bool_t ret = !FT_Get_Glyph_Name(ftFace, glyph, name, size);
	if (ret && (size && !*name)) {
		ret = false;
	}

	return ret;
}

static hb_bool_t hb_ft_get_glyph_from_name(hb_font_t* /*font*/, void* fontData, const char* name, int len,
		hb_codepoint_t* pGlyph, void* /*userData*/) {
	auto* pImpl = reinterpret_cast<const HarfbuzzFontImpl*>(fontData);
	FT_Face ftFace = pImpl->ftFace;

	if (len < 0) {
		*pGlyph = FT_Get_Name_Index(ftFace, (FT_String*)name);
	}
	else {
		/* Make a nul-terminated version. */
		char buf[128];
		len = hb_min(len, (int)sizeof(buf) - 1);
		strncpy(buf, name, len);
		buf[len] = '\0';
		*pGlyph = FT_Get_Name_Index(ftFace, buf);
	}

	if (*pGlyph == 0) {
		/* Check whether the given name was actually the name of glyph 0. */
		char buf[128];
		if (!FT_Get_Glyph_Name(ftFace, 0, buf, sizeof (buf)) &&
				len < 0 ? !strcmp(buf, name) : !strncmp(buf, name, len)) {
		  return true;
		}
	}

	return *pGlyph != 0;
}

static hb_bool_t hb_ft_get_font_h_extents(hb_font_t* font, void* fontData, hb_font_extents_t* pMetrics,
		void* /*userData*/) {
	auto* pImpl = reinterpret_cast<const HarfbuzzFontImpl*>(fontData);
	FT_Face ftFace = pImpl->ftFace;
	float y_mult;

#ifdef HAVE_FT_GET_TRANSFORM
  if (ft_font->transform)
  {
	FT_Matrix matrix;
	FT_Get_Transform (ft_face, &matrix, nullptr);
	y_mult = sqrtf ((float)matrix.yx * matrix.yx + (float)matrix.yy * matrix.yy) / 65536.f;
	y_mult *= font->y_scale < 0 ? -1 : +1;
  }
  else
#endif
  {
	y_mult = font->y_scale < 0 ? -1 : +1;
  }

	if (ftFace->units_per_EM != 0) {
		pMetrics->ascender = FT_MulFix(ftFace->ascender, ftFace->size->metrics.y_scale);
		pMetrics->descender = FT_MulFix(ftFace->descender, ftFace->size->metrics.y_scale);
		pMetrics->line_gap = FT_MulFix(ftFace->height, ftFace->size->metrics.y_scale )
				- (pMetrics->ascender - pMetrics->descender);
	}
	else {
		/* Bitmap-only font, eg. color bitmap font. */
		pMetrics->ascender = ftFace->size->metrics.ascender;
		pMetrics->descender = ftFace->size->metrics.descender;
		pMetrics->line_gap = ftFace->size->metrics.height - (pMetrics->ascender - pMetrics->descender);
	}

	pMetrics->ascender  = (hb_position_t)(y_mult * (pMetrics->ascender + font->y_strength));
	pMetrics->descender = (hb_position_t)(y_mult * pMetrics->descender);
	pMetrics->line_gap  = (hb_position_t)(y_mult * pMetrics->line_gap);

	return true;
}
