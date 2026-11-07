#include "prx/libcairo/include/FontFace.hpp"
#include "prx/libcairo/include/FontOptions.hpp"
#include "prx/libcairo/include/InventoryExports.hpp"
#include "prx/libcairo/include/Matrix.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <new>

namespace {

constexpr int kSuccess = 0;
constexpr int kNoMemory = 1;
constexpr int kInvalidMatrix = 5;
constexpr int kNullPointer = 7;
constexpr int kErrorCount = 256;

struct CairoScaledFontState {
    std::atomic<unsigned> references{1};
    int status{kSuccess};
    cairo_font_face_t* face{};
    CairoMatrix fontMatrix{1, 0, 0, 1, 0, 0};
    CairoMatrix ctm{1, 0, 0, 1, 0, 0};
    CairoFontOptionsValues options{};
    bool immortal{};

    CairoScaledFontState() = default;
    CairoScaledFontState(int error, bool permanent) : status(error), immortal(permanent) {}
};

CairoScaledFontState* State(cairo_scaled_font_t* scaledFont) {
    return reinterpret_cast<CairoScaledFontState*>(scaledFont);
}

cairo_scaled_font_t* Handle(CairoScaledFontState* state) {
    return reinterpret_cast<cairo_scaled_font_t*>(state);
}

CairoScaledFontState* ErrorState(int status) {
    static std::array<CairoScaledFontState, kErrorCount> states;
    static const bool initialized = [] {
        for (int index = 0; index != kErrorCount; ++index) {
            states[index].status = index;
            states[index].immortal = true;
        }
        return true;
    }();
    static_cast<void>(initialized);
    return status >= 0 && status < kErrorCount ? &states[status] : &states[kNoMemory];
}

bool Finite(const CairoMatrix& matrix) {
    return std::isfinite(matrix.xx) && std::isfinite(matrix.yx) && std::isfinite(matrix.xy) &&
           std::isfinite(matrix.yy) && std::isfinite(matrix.x0) && std::isfinite(matrix.y0);
}

void SetOptions(cairo_font_options_t* options, const CairoFontOptionsValues& values) {
    cairo_font_options_set_antialias(options, values.antialias);
    cairo_font_options_set_subpixel_order(options, values.subpixelOrder);
    cairo_font_options_set_hint_style(options, values.hintStyle);
    cairo_font_options_set_hint_metrics(options, values.hintMetrics);
}

}

extern "C" {

cairo_scaled_font_t* APS5_VABI cairo_scaled_font_create(cairo_font_face_t* fontFace, const CairoMatrix* fontMatrix,
                                                       const CairoMatrix* ctm, const cairo_font_options_t* options) {
    if (!fontFace) return Handle(ErrorState(kNullPointer));
    const int faceStatus = CairoInternal::FontFaceStatus(fontFace);
    if (faceStatus != kSuccess) return Handle(ErrorState(faceStatus));
    if (!fontMatrix || !ctm) return Handle(ErrorState(kNullPointer));
    if (!Finite(*fontMatrix) || !Finite(*ctm)) return Handle(ErrorState(kInvalidMatrix));
    if (!options) return Handle(ErrorState(kNullPointer));

    CairoFontOptionsValues optionValues{};
    if (!CairoFontOptionsRead(options, optionValues)) return Handle(ErrorState(kNoMemory));

    auto* state = new (std::nothrow) CairoScaledFontState;
    if (!state) return Handle(ErrorState(kNoMemory));

    state->face = cairo_font_face_reference(fontFace);
    state->fontMatrix = *fontMatrix;
    state->ctm = *ctm;
    state->ctm.x0 = 0;
    state->ctm.y0 = 0;
    state->options = optionValues;
    return Handle(state);
}

cairo_scaled_font_t* APS5_VABI cairo_scaled_font_reference(cairo_scaled_font_t* scaledFont) {
    if (!scaledFont) return nullptr;
    auto* state = State(scaledFont);
    if (state->immortal) return scaledFont;

    auto references = state->references.load(std::memory_order_relaxed);
    while (references != 0 && references != std::numeric_limits<unsigned>::max()) {
        if (state->references.compare_exchange_weak(references, references + 1, std::memory_order_relaxed,
                                                    std::memory_order_relaxed))
            break;
    }
    return scaledFont;
}

void APS5_VABI cairo_scaled_font_destroy(cairo_scaled_font_t* scaledFont) {
    if (!scaledFont) return;
    auto* state = State(scaledFont);
    if (state->immortal) return;

    auto references = state->references.load(std::memory_order_relaxed);
    if (references == std::numeric_limits<unsigned>::max()) return;
    while (references != 0 && !state->references.compare_exchange_weak(
                                  references, references - 1, std::memory_order_acq_rel,
                                  std::memory_order_relaxed)) {}
    if (references != 1) return;

    auto* face = state->face;
    delete state;
    cairo_font_face_destroy(face);
}

cairo_status_t APS5_VABI cairo_scaled_font_status(cairo_scaled_font_t* scaledFont) {
    return static_cast<cairo_status_t>(scaledFont ? State(scaledFont)->status : kNullPointer);
}

cairo_font_face_t* APS5_VABI cairo_scaled_font_get_font_face(cairo_scaled_font_t* scaledFont) {
    if (!scaledFont) return CairoInternal::FontFaceNoMemory();
    auto* state = State(scaledFont);
    return state->status == kSuccess ? state->face : CairoInternal::FontFaceNoMemory();
}

void APS5_VABI cairo_scaled_font_get_font_matrix(cairo_scaled_font_t* scaledFont, CairoMatrix* fontMatrix) {
    if (!fontMatrix) return;
    if (!scaledFont || State(scaledFont)->status != kSuccess) {
        *fontMatrix = {1, 0, 0, 1, 0, 0};
        return;
    }
    *fontMatrix = State(scaledFont)->fontMatrix;
}

void APS5_VABI cairo_scaled_font_get_ctm(cairo_scaled_font_t* scaledFont, CairoMatrix* ctm) {
    if (!ctm) return;
    if (!scaledFont || State(scaledFont)->status != kSuccess) {
        *ctm = {1, 0, 0, 1, 0, 0};
        return;
    }
    *ctm = State(scaledFont)->ctm;
}

void APS5_VABI cairo_scaled_font_get_font_options(cairo_scaled_font_t* scaledFont, cairo_font_options_t* options) {
    if (!options) return;
    if (!scaledFont || State(scaledFont)->status != kSuccess) {
        SetOptions(options, {});
        return;
    }
    SetOptions(options, State(scaledFont)->options);
}

}
