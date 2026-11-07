#pragma once

#include "prx/libcairo/include/InventoryExports.hpp"

namespace CairoInternal {

int FontFaceStatus(cairo_font_face_t* face);
cairo_font_face_t* FontFaceNoMemory();

}
