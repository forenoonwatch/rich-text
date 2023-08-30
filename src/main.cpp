#include <cstdio>

#include "file_read_bytes.hpp"
#include "font_cache.hpp"
#include "text_box.hpp"

#include <MiniFB.h>

static Bitmap g_bitmap;
static TextBox g_textBox;

static void on_resize(mfb_window* window, int width, int height);

int main() {
	FontCache fontCache("fonts/families");
	auto famIdx = fontCache.get_font_family("Noto Sans"); 
	auto font = fontCache.get_font(famIdx, FontWeightIndex::REGULAR, FontFaceStyle::NORMAL, 24);

	g_textBox.set_rich_text(true);

	{
		std::vector<char> fileData;
		if (fileData = file_read_bytes("Sample.txt"); fileData.empty()) {
			puts("File Sample.txt must be present!");
			return 1;
		}

		std::string str(fileData.data(), fileData.size());

		g_textBox.set_text(std::move(str));
	}

	g_textBox.set_font(std::move(font));
	g_textBox.set_position(0.f, 30.f);

	auto* window = mfb_open_ex("Font Tests", 640, 480, WF_RESIZABLE);
	on_resize(window, 640, 480);

	mfb_set_resize_callback(window, on_resize);

	do {
		if (mfb_update_ex(window, g_bitmap.data(), g_bitmap.get_width(), g_bitmap.get_height()) < 0) {
			window = nullptr;
			break;
		}
	}
	while (mfb_wait_sync(window));
}

static void on_resize(mfb_window* window, int width, int height) {
	g_textBox.set_size(static_cast<float>(width), static_cast<float>(height));

	g_bitmap = Bitmap(width, height);
	g_bitmap.clear({1.f, 1.f, 1.f, 1.f});

	g_textBox.render(g_bitmap);
}

