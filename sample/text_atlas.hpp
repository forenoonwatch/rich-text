#pragma once

#include "font.hpp"
#include "image.hpp"
#include "stroke_type.hpp"

#include <memory>
#include <vector>
#include <unordered_map>

namespace Text { class Bitmap; }
namespace Text { struct FontRasterizeInfo; }

class TextAtlas final {
	public:
		explicit TextAtlas();

		Image* get_glyph_info(Text::SingleScriptFont, uint32_t glyphIndex, float* texCoordExtentsOut,
				float* sizeOut, float* offsetOut, bool& hasColorOut);
		Image* get_stroke_info(Text::SingleScriptFont, uint32_t glyphIndex, uint8_t thickness,
				Text::StrokeType strokeType, float* texCoordExtentsOut, float* sizeOut, float* offsetOut,
				bool& hasColorOut);

		Image* get_default_texture();
	private:
		struct Page {
			Image image;
			uint32_t xOffset;
			uint32_t yOffset;
			uint32_t lineHeight;
			bool hasColor;
		};

		struct GlyphInfo {
			float texCoordExtents[4];
			float bitmapSize[2];
			float offset[2];
			uint32_t pageIndex;
			Page* pPage;
		};

		struct GlyphKey {
			uint32_t size;
			uint32_t glyphIndex;
			Text::FaceIndex_T face;
			Text::FontWeight weight;
			Text::FontStyle style;

			bool operator==(const GlyphKey&) const;
		};

		struct GlyphKeyHash {
			size_t operator()(const GlyphKey&) const;
		};

		struct StrokeKey {
			uint32_t glyphSize;
			uint32_t glyphIndex;
			Text::FaceIndex_T face;
			uint8_t strokeSize;
			Text::StrokeType type;

			bool operator==(const StrokeKey&) const;
		};

		struct StrokeKeyHash {
			size_t operator()(const StrokeKey&) const;
		};

		std::vector<std::unique_ptr<Page>> m_pages;
		std::unordered_map<GlyphKey, GlyphInfo, GlyphKeyHash> m_glyphs;
		std::unordered_map<StrokeKey, GlyphInfo, StrokeKeyHash> m_strokes;

		Image m_defaultImage;

		void handle_rasterization(const Text::FontRasterizeInfo&, GlyphInfo&, bool& hasColor);

		Page* upload_glyph(const Text::Bitmap&, float* texCoordExtentsOut, bool hasColor);
		Page* get_or_create_target_page(uint32_t width, uint32_t height, bool hasColor);

		static bool page_can_fit_glyph(Page&, uint32_t width, uint32_t height);
};

inline TextAtlas* g_textAtlas{};

