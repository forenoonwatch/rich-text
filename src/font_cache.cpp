#include "font_cache.hpp"

#include "file_read_bytes.hpp"
#include "font.hpp"

#include <simdjson.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb-ft.h>

FontCache::FontCache(std::filesystem::path root) {
	FT_Init_FreeType(&m_ftLibrary);

	for (auto& entry : std::filesystem::directory_iterator{root}) {
		if (entry.path().extension().compare(".json") == 0) {
			load_family_file(entry.path());
		}
	}
}

FontCache::~FontCache() {
	for (auto& face : m_faces) {
		for (auto [size, pFont] : face.fonts) {
			delete pFont;
		}
	}

	FT_Done_FreeType(m_ftLibrary);
}

FamilyIndex_T FontCache::get_font_family(std::string_view name) const {
	if (auto it = m_familiesByName.find(name); it != m_familiesByName.end()) {
		return it->second;
	}

	return INVALID_FAMILY;
}

FaceIndex_T FontCache::get_face_index(FamilyIndex_T family, FontWeightIndex weight, FontFaceStyle style,
		UScriptCode script) const {
	return m_scriptFaceLookup[family].lookup[static_cast<size_t>(weight)][static_cast<size_t>(style)][script];
}

MultiScriptFont FontCache::get_font(FamilyIndex_T family, FontWeightIndex weight, FontFaceStyle style,
		uint32_t size) {
	return MultiScriptFont(*this, family, weight, style, size);
}

Font* FontCache::get_font_for_script(FamilyIndex_T family, FontWeightIndex weight, FontFaceStyle style,
		UScriptCode script, uint32_t size) {
	auto faceIndex = get_face_index(family, weight, style, script);
	return try_create_font(faceIndex, family, weight, style, size);;
}

Font* FontCache::get_fallback_font(FamilyIndex_T family, FaceIndex_T fallbackIndex, uint32_t size) {
	auto faceIndex = m_fallbackFaces[fallbackIndex];
	return try_create_font(faceIndex, family, FontWeightIndex::REGULAR, FontFaceStyle::NORMAL, size);
}

bool FontCache::face_has_script(FamilyIndex_T family, FontWeightIndex weight, FontFaceStyle style,
		FaceIndex_T face, UScriptCode script) const {
	return get_face_index(family, weight, style, script) == face;
}

FontCache::FamilyFallbackInfo FontCache::get_fallback_info(FamilyIndex_T family) const {
	return m_familyFallbackInfos[family];
}

// Private

bool FontCache::try_init_face(FontFace& face) {
	if (!face.fileData.empty()) {
		return true;
	}

	face.fileData = file_read_bytes(face.fileName.c_str());

	return !face.fileData.empty();
}

Font* FontCache::try_create_font(FaceIndex_T faceIndex, FamilyIndex_T family, FontWeightIndex weight,
		FontFaceStyle style, uint32_t size) {
	auto& face = m_faces[faceIndex];

	if (auto it = face.fonts.find(size); it != face.fonts.end()) {
		return it->second;
	}

	if (!try_init_face(face)) {
		return nullptr;
	}

	FT_Face ftFace;
	if (FT_New_Memory_Face(m_ftLibrary, reinterpret_cast<const FT_Byte*>(face.fileData.data()),
			face.fileData.size(), 0, &ftFace) != 0) {
		return nullptr;
	}

	FT_Size_RequestRec sr{
		.type = FT_SIZE_REQUEST_TYPE_REAL_DIM,
		.height = static_cast<FT_Long>(size) * 64,
	};
	FT_Request_Size(ftFace, &sr);

	hb_font_t* hbFont = hb_ft_font_create(ftFace, nullptr);

	if (!hbFont) {
		FT_Done_Face(ftFace);
		return nullptr;
	}

	hb_ft_font_set_load_flags(hbFont, FT_LOAD_DEFAULT);

	auto* font = new Font(*this, ftFace, hbFont, faceIndex, family, weight, style, size);
	face.fonts.emplace(std::make_pair(size, font));

	return font;
}

bool FontCache::load_family_file(const std::filesystem::path& path) {
	auto familyIndex = static_cast<FamilyIndex_T>(m_scriptFaceLookup.size());
	m_scriptFaceLookup.emplace_back();
	auto& scriptFaceLookup = m_scriptFaceLookup.back();

	for (size_t weight = 0; weight < WEIGHT_COUNT; ++weight) {
		for (size_t style = 0; style < STYLE_COUNT; ++style) {
			for (size_t script = 0; script < USCRIPT_CODE_LIMIT; ++script) {
				scriptFaceLookup.lookup[weight][style][script] = INVALID_FONT;
			}
		}
	}

	auto fileData = file_read_bytes(path.string().c_str());
	fileData.resize(fileData.size() + simdjson::SIMDJSON_PADDING);
	simdjson::padded_string_view sv(fileData.data(), fileData.size());
	simdjson::ondemand::parser parser;
	auto d = parser.iterate(sv);

	simdjson::ondemand::object root;
	if (d.get(root)) {
		return false;
	}

	std::string_view familyName;
	if (root["name"].get(familyName)) {
		return false;
	}

	simdjson::ondemand::array faces;
	if (root["faces"].get(faces)) {
		return false;
	}

	for (auto faceValue : faces) {
		simdjson::ondemand::object fontInfo;
		if (auto error = faceValue.get(fontInfo); error) {
			puts(simdjson::error_message(error));
			return false;
		}

		uint64_t weight;
		if (fontInfo["weight"].get(weight)) {
			return false;
		}

		if (weight < 100 || weight > 900 || weight % 100 != 0) {
			return false;
		}

		auto enumWeight = static_cast<FontWeight>(weight);
		auto weightIndex = weight / 100 - 1;

		std::string_view styleName;
		if (fontInfo["style"].get(styleName)) {
			return false;
		}

		auto enumStyle = FontFaceStyle::NORMAL;

		if (styleName.compare("italic") == 0) {
			enumStyle = FontFaceStyle::ITALIC;
		}
		else if (styleName.compare("normal") != 0) {
			return false;
		}

		auto styleIndex = static_cast<size_t>(enumStyle);

		simdjson::ondemand::array scriptData;
		if (fontInfo["scripts"].get(scriptData)) {
			return false;
		}

		auto defaultFaceIndex = INVALID_FONT;

		for (auto scriptFileValue : scriptData) {
			simdjson::ondemand::object scriptInfo;
			if (scriptFileValue.get(scriptInfo)) {
				return false;
			}

			std::string_view uri;
			if (scriptInfo["uri"].get(uri)) {
				return false;
			}

			auto faceIndex = static_cast<FaceIndex_T>(m_faces.size());

			m_faces.push_back({
				.fileName = std::string(uri),
			});

			simdjson::ondemand::array scriptList;
			if (!scriptInfo["scripts"].get(scriptList)) {
				for (auto scriptValue : scriptList) {
					uint64_t scriptCodeValue;
					if (scriptValue.get(scriptCodeValue)) {
						return false;
					}

					scriptFaceLookup.lookup[weightIndex][styleIndex][scriptCodeValue] = faceIndex;
				}
			}
			else {
				defaultFaceIndex = faceIndex;
			}
		}

		// Apply default font to all scripts that are unset
		if (defaultFaceIndex != INVALID_FONT) {
			auto& facesByScript = scriptFaceLookup.lookup[weightIndex][styleIndex];

			for (size_t i = 0; i < USCRIPT_CODE_LIMIT; ++i) {
				if (facesByScript[i] == INVALID_FONT) {
					facesByScript[i] = defaultFaceIndex;
				}
			}
		}
	}

	m_familyFallbackInfos.emplace_back();
	auto& fallbackInfo = m_familyFallbackInfos.back();
	fallbackInfo.baseIndex = static_cast<FaceIndex_T>(m_fallbackFaces.size());
	fallbackInfo.count = 0;

	simdjson::ondemand::array fallbackFaces;
	if (!root["fallback_faces"].get(fallbackFaces)) {
		for (auto faceValue : fallbackFaces) {
			simdjson::ondemand::object fontInfo;
			if (faceValue.get(fontInfo)) {
				return false;
			}

			std::string_view uri;
			if (fontInfo["uri"].get(uri)) {
				return false;
			}

			m_fallbackFaces.emplace_back(static_cast<FaceIndex_T>(m_faces.size()));

			m_faces.push_back({
				.fileName = std::string(uri),
			});

			++fallbackInfo.count;
		}
	}

	m_familiesByName.emplace(std::make_pair(std::string(familyName), familyIndex));

	return true;
}

