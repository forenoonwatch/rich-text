#include "font_registry.hpp"

#include "file_read_bytes.hpp"

#include <simdjson.h>

#include <bitset>
#include <filesystem>

using namespace Text;

// Public Functions

FontRegistryError FontRegistry::register_families_from_path(const char* pathName) {
	for (auto& entry : std::filesystem::directory_iterator{pathName}) {
		if (entry.path().extension().compare(".json") == 0) {
			if (auto res = FontRegistry::register_family_from_json_file(entry.path().string().c_str());
					res != FontRegistryError::NONE) {
				return res;
			}
		}
	}

	return FontRegistryError::NONE;
}

FontRegistryError FontRegistry::register_family_from_json_file(const char* uri) {
	auto fileData = file_read_bytes(uri);
	fileData.resize(fileData.size() + simdjson::SIMDJSON_PADDING);
	return register_family_from_json_data(std::string_view(fileData.data(), fileData.size()));
}

FontRegistryError FontRegistry::register_family_from_json_data(std::string_view fileData) {
	simdjson::padded_string_view sv(fileData.data(), fileData.size());
	simdjson::ondemand::parser parser;
	auto d = parser.iterate(sv);

	simdjson::ondemand::object root;
	if (d.get(root) != 0) {
		return FontRegistryError::INVALID_JSON;
	}

	std::string_view familyName;
	if (root["name"].get(familyName) != 0) {
		return FontRegistryError::INVALID_JSON;
	}

	std::bitset<USCRIPT_CODE_LIMIT> availableScripts;
	std::vector<std::string_view> linkedFamilies;
	std::vector<std::string_view> fallbackFamilies;
	std::vector<FontFaceCreateInfo> faces;
	bool foundScripts = false;

	simdjson::ondemand::array scripts;
	if (foundScripts = (root["scripts"].get(scripts) == 0); foundScripts) {
		for (auto scriptValue : scripts) {
			std::string_view scriptName;
			int64_t scriptCode;

			if (scriptValue.get(scriptCode) == 0) {
				if (scriptCode >= USCRIPT_CODE_LIMIT) {
					return FontRegistryError::INVALID_JSON;
				}

				availableScripts.set(scriptValue);
			}
			else if (scriptValue.get(scriptName) == 0) {
				UErrorCode err{};
				UScriptCode code;
				uscript_getCode(std::string(scriptName).c_str(), &code, 1, &err);

				if (U_FAILURE(err)) {
					return FontRegistryError::INVALID_JSON;
				}

				availableScripts.set(code);
			}
			else {
				return FontRegistryError::INVALID_JSON;
			}
		}
	}

	simdjson::ondemand::array linkedFamilyArray;
	if (root["linked_families"].get(linkedFamilyArray) == 0) {
		for (auto familyValue : linkedFamilyArray) {
			std::string_view familyName;
			if (familyValue.get(familyName) != 0) {
				return FontRegistryError::INVALID_JSON;
			}

			linkedFamilies.emplace_back(std::move(familyName));
		}
	}

	simdjson::ondemand::array fallbackFamilyArray;
	if (root["fallback_families"].get(fallbackFamilyArray) == 0) {
		for (auto familyValue : fallbackFamilyArray) {
			std::string_view familyName;
			if (familyValue.get(familyName) != 0) {
				return FontRegistryError::INVALID_JSON;
			}

			fallbackFamilies.emplace_back(std::move(familyName));
		}
	}

	simdjson::ondemand::array faceArray;
	if (root["faces"].get(faceArray) != 0) {
		return FontRegistryError::INVALID_JSON;
	}

	for (auto faceValue : faceArray) {
		simdjson::ondemand::object faceObject;
		if (faceValue.get(faceObject) != 0) {
			return FontRegistryError::INVALID_JSON;
		}

		faces.emplace_back();
		auto& face = faces.back();
		int64_t weight;

		if (faceObject["name"].get(face.name) != 0) {
			return FontRegistryError::INVALID_JSON;
		}

		if (faceObject["uri"].get(face.uri) != 0) {
			return FontRegistryError::INVALID_JSON;
		}

		if (faceObject["weight"].get(weight) != 0) {
			return FontRegistryError::INVALID_JSON;
		}

		if (weight < 100 || weight > 900 || weight % 100 != 0) {
			return FontRegistryError::INVALID_JSON;
		}

		face.weight = static_cast<FontWeight>((weight - 100) / 100);

		std::string_view style;
		if (faceObject["style"].get(style) != 0) {
			return FontRegistryError::INVALID_JSON;
		}

		if (style.compare("normal") != 0 && style.compare("italic") != 0) {
			return FontRegistryError::INVALID_JSON;
		}

		face.style = style.compare("italic") == 0 ? FontStyle::ITALIC : FontStyle::NORMAL;
	}

	std::vector<UScriptCode> scriptCodes;
	
	if (foundScripts) {
		for (size_t i = 0; i < USCRIPT_CODE_LIMIT; ++i) {
			if (availableScripts.test(i)) {
				scriptCodes.emplace_back(static_cast<UScriptCode>(i));
			}
		}
	}

	FontFamilyCreateInfo familyInfo{
		.name = familyName,
		.pScriptCodes = scriptCodes.data(),
		.scriptCodeCount = static_cast<uint32_t>(scriptCodes.size()),
		.pLinkedFamilies = linkedFamilies.data(),
		.linkedFamilyCount = static_cast<uint32_t>(linkedFamilies.size()),
		.pFallbackFamilies = fallbackFamilies.data(),
		.fallbackFamilyCount = static_cast<uint32_t>(fallbackFamilies.size()),
		.pFaces = faces.data(),
		.faceCount = static_cast<uint32_t>(faces.size()),
	};

	return FontRegistry::register_family(familyInfo);
}

