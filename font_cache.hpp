#pragma once

#include "font_common.hpp"
#include "string_hash.hpp"

#include <unicode/uscript.h>

#include <filesystem>
#include <vector>
#include <unordered_map>

struct FT_LibraryRec_;

class Font;

class FontCache final {
	private:
		static constexpr const size_t WEIGHT_COUNT = static_cast<size_t>(FontWeightIndex::COUNT);
		static constexpr const size_t STYLE_COUNT = static_cast<size_t>(FontFaceStyle::COUNT);
	public:
		static constexpr const FamilyIndex_T INVALID_FAMILY = static_cast<FamilyIndex_T>(~0u);
		static constexpr const FaceIndex_T INVALID_FONT = static_cast<FaceIndex_T>(~0u);

		explicit FontCache(std::filesystem::path root);
		~FontCache();

		FamilyIndex_T get_font_family(std::string_view name) const;
		FaceIndex_T get_face_index(FamilyIndex_T, FontWeightIndex, FontFaceStyle, UScriptCode) const;

		Font* get_font(FamilyIndex_T, FontWeightIndex, FontFaceStyle, UScriptCode, uint32_t size);

		bool face_has_script(FamilyIndex_T, FontWeightIndex, FontFaceStyle, FaceIndex_T, UScriptCode) const;
	private:
		struct FontFace {
			std::unordered_map<uint32_t, Font*> fonts;
			std::vector<char> fileData;
			std::string fileName;
		};

		struct FontFamily {
			FaceIndex_T faceLookup[WEIGHT_COUNT][STYLE_COUNT][USCRIPT_CODE_LIMIT];
		};

		std::vector<FontFace> m_faces;
		std::vector<FontFamily> m_families;
		std::unordered_map<std::string, FamilyIndex_T, StringHash, std::equal_to<>> m_familiesByName;

		FT_LibraryRec_* m_ftLibrary;

		bool try_init_face(FontFace&);

		bool load_family_file(const std::filesystem::path&);
};

