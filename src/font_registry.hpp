#pragma once

#include "font.hpp"
#include "font_data.hpp"
#include "file_mapping.hpp"

#include <unicode/uscript.h>

namespace Text {

struct FontFaceCreateInfo {
	std::string_view name;
	std::string_view uri;
	FontWeight weight;
	FontStyle style;
};

struct FontFamilyCreateInfo {
	std::string_view name;
	const UScriptCode* pScriptCodes;
	uint32_t scriptCodeCount;
	const std::string_view* pLinkedFamilies;
	uint32_t linkedFamilyCount;
	const std::string_view* pFallbackFamilies;
	uint32_t fallbackFamilyCount;
	const FontFaceCreateInfo* pFaces;
	uint32_t faceCount;
};

enum class FontRegistryError {
	NONE,
	ALREADY_LOADED,
	NO_FACES,
	INVALID_JSON,
};

}

namespace Text::FontRegistry {

/**
 * Gets a handle for the font family with the given name. Returns an invalid handle if the family
 * does not exist or has not been initialized.
 *
 * @thread_safety Thread safe, may block internally.
 */
[[nodiscard]] FontFamily get_family(std::string_view name);

/**
 * Gets the face handle corresponding to the given font handle.
 * Must be called with a valid font handle.
 *
 * @thread_safety Thread safe, may block internally.
 */
[[nodiscard]] FontFace get_face(Font font);

/**
 * Gets a temporary handle to FreeType and HarfBuzz data structures representing the given font face. Object
 * may be invalid if the face handle is invalid or underlying font data failed to load at any point.
 *
 * @thread_safety Thread safe, see `FontData` for caveats about threading and lifetimes with the returned object.
 */
[[nodiscard]] FontData get_font_data(FontFace, uint32_t size, FontWeight targetWeight, FontStyle targetStyle);
[[nodiscard]] FontData get_font_data(Font);
[[nodiscard]] FontData get_font_data(SingleScriptFont);

/**
 * Registers a new font family based on the provided `FontFamilyCreateInfo`. If a family name referenced in
 * `pLinkedFamilies` or `pFallbackFamilies` has not yet been loaded, a family handle will be reserved for that
 * name. If successfully loaded, font families remain registered until program termination.
 *
 * If `scriptCodeCount` is 0, the family is assumed to cover all scripts.
 *
 * `pFaces` *must* not be null and contain at least one face.
 * All faces must have a globally unique name across all families.
 * Each face provided for a single family must have a unique weight and style.
 * Faces *may* share the same URI.
 *
 * @thread_safety Thread safe, may block internally.
 */
[[nodiscard]] FontRegistryError register_family(const FontFamilyCreateInfo& familyInfo);

/**
 * Registers family data from JSON data in memory. Data is assumed to have `SIMDJSON_PADDING` extra padding
 * bytes reflected in its size (so fileData.size() - SIMDJSON_PADDING is the actual data size).
 */
[[nodiscard]] FontRegistryError register_family_from_json_data(std::string_view fileData);

/**
 * Registers family data from a JSON file located at `uri`.
 */
[[nodiscard]] FontRegistryError register_family_from_json_file(const char* uri);

/**
 * Registers family data from all JSON files located directly under `path`.
 */
[[nodiscard]] FontRegistryError register_families_from_path(const char* path);

/**
 * Gets a descriptor for a font face and size that can be used to display the given text up to `offset`, based on
 * the provided base font and script. Output is valid only if the given text's script matches the provided
 * script code.
 *
 * For use in generating text layout.
 *
 * @thread_safety Thread safe, may block internally.
 */
[[nodiscard]] SingleScriptFont get_sub_font(Font font, const char* text, int32_t& offset, int32_t limit, 
		UScriptCode script);
[[nodiscard]] SingleScriptFont get_sub_font(Font font, const char16_t* text, int32_t& offset, int32_t limit, 
		UScriptCode script);

/**
 * Sets the file mapping functions used to load font files internally. This function can only be called before
 * the `FontRegistry` has begun to be used to load fonts. Changing the mapping functions once files have already
 * been loaded will result in undefined behavior.
 *
 *
 * @thread_safety This function must be externally synchronized.
 * @see FileMapping
 */
void set_file_mapping_functions(const FileMappingFunctions& funcs);

}

