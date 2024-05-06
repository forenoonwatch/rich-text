#pragma once

#include <hb.h>

struct FT_FaceRec_;

namespace Text {

hb_font_t* harfbuzz_font_create(FT_FaceRec_* ftFace);
void harfbuzz_font_mark_changed(hb_font_t*);

}

