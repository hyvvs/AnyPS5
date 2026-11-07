#include "FreeTypeFontFixture.hpp"
#include "prx/libcairo/include/InventoryExports.hpp"
#include "prx/libcairo/include/Matrix.hpp"
#include "prx/libSceFreeType/libSceFreeType.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#if defined(__linux__)
#include <dlfcn.h>
#include <link.h>
#endif

namespace {

struct OptionsValues {
    cairo_antialias_t antialias;
    cairo_subpixel_order_t subpixelOrder;
    cairo_hint_style_t hintStyle;
    cairo_hint_metrics_t hintMetrics;
};

static_assert(sizeof(OptionsValues) == sizeof(int) * 4);
using ScaledFontCreateSignature = cairo_scaled_font_t* (APS5_VABI *)(cairo_font_face_t*, const cairo_matrix_t*,
                                                                     const cairo_matrix_t*,
                                                                     const cairo_font_options_t*);
using ScaledFontReferenceSignature = cairo_scaled_font_t* (APS5_VABI *)(cairo_scaled_font_t*);
using ScaledFontDestroySignature = void (APS5_VABI *)(cairo_scaled_font_t*);
using ScaledFontStatusSignature = cairo_status_t (APS5_VABI *)(cairo_scaled_font_t*);
using ScaledFontGetFaceSignature = cairo_font_face_t* (APS5_VABI *)(cairo_scaled_font_t*);
using ScaledFontGetMatrixSignature = void (APS5_VABI *)(cairo_scaled_font_t*, cairo_matrix_t*);
using ScaledFontGetCtmSignature = void (APS5_VABI *)(cairo_scaled_font_t*, cairo_matrix_t*);
using ScaledFontGetOptionsSignature = void (APS5_VABI *)(cairo_scaled_font_t*, cairo_font_options_t*);
static_assert(std::is_same_v<decltype(&cairo_scaled_font_create), ScaledFontCreateSignature>);
static_assert(std::is_same_v<decltype(&cairo_scaled_font_reference), ScaledFontReferenceSignature>);
static_assert(std::is_same_v<decltype(&cairo_scaled_font_destroy), ScaledFontDestroySignature>);
static_assert(std::is_same_v<decltype(&cairo_scaled_font_status), ScaledFontStatusSignature>);
static_assert(std::is_same_v<decltype(&cairo_scaled_font_get_font_face), ScaledFontGetFaceSignature>);
static_assert(std::is_same_v<decltype(&cairo_scaled_font_get_font_matrix), ScaledFontGetMatrixSignature>);
static_assert(std::is_same_v<decltype(&cairo_scaled_font_get_ctm), ScaledFontGetCtmSignature>);
static_assert(std::is_same_v<decltype(&cairo_scaled_font_get_font_options), ScaledFontGetOptionsSignature>);
static_assert(std::is_same_v<decltype(cairo_scaled_font_create(nullptr, nullptr, nullptr, nullptr)), cairo_scaled_font_t*>);
static_assert(std::is_same_v<decltype(cairo_scaled_font_reference(nullptr)), cairo_scaled_font_t*>);
static_assert(std::is_same_v<decltype(cairo_scaled_font_destroy(nullptr)), void>);
static_assert(std::is_same_v<decltype(cairo_scaled_font_status(nullptr)), cairo_status_t>);
static_assert(std::is_same_v<decltype(cairo_scaled_font_get_font_face(nullptr)), cairo_font_face_t*>);
static_assert(std::is_same_v<decltype(cairo_scaled_font_get_font_matrix(nullptr, nullptr)), void>);
static_assert(std::is_same_v<decltype(cairo_scaled_font_get_ctm(nullptr, nullptr)), void>);
static_assert(std::is_same_v<decltype(cairo_scaled_font_get_font_options(nullptr, nullptr)), void>);

unsigned faceDestroyCount{};

void Require(bool valid, const char* reason) {
    if (valid) return;
    std::fprintf(stderr, "cairo scaled font: %s\n", reason);
    std::abort();
}

void APS5_VABI RecordFaceDestroy(void*) {
    ++faceDestroyCount;
}

OptionsValues ReadOptions(const cairo_font_options_t* options) {
    OptionsValues values{};
    std::memcpy(&values, options, sizeof(values));
    return values;
}

void SetOptions(cairo_font_options_t* options, OptionsValues values) {
    cairo_font_options_set_antialias(options, values.antialias);
    cairo_font_options_set_subpixel_order(options, values.subpixelOrder);
    cairo_font_options_set_hint_style(options, values.hintStyle);
    cairo_font_options_set_hint_metrics(options, values.hintMetrics);
}

void RequireOptions(OptionsValues actual, OptionsValues expected, const char* reason) {
    Require(actual.antialias == expected.antialias, reason);
    Require(actual.subpixelOrder == expected.subpixelOrder, reason);
    Require(actual.hintStyle == expected.hintStyle, reason);
    Require(actual.hintMetrics == expected.hintMetrics, reason);
}

void RequireMatrix(const CairoMatrix& actual, const CairoMatrix& expected, const char* reason) {
    Require(actual.xx == expected.xx && actual.yx == expected.yx && actual.xy == expected.xy &&
                actual.yy == expected.yy && actual.x0 == expected.x0 && actual.y0 == expected.y0,
            reason);
}

constexpr CairoMatrix Identity{1, 0, 0, 1, 0, 0};
constexpr OptionsValues Defaults{CAIRO_ANTIALIAS_DEFAULT, CAIRO_SUBPIXEL_ORDER_DEFAULT,
                                 CAIRO_HINT_STYLE_DEFAULT, CAIRO_HINT_METRICS_DEFAULT};
constexpr OptionsValues InitialOptions{CAIRO_ANTIALIAS_SUBPIXEL, CAIRO_SUBPIXEL_ORDER_VBGR,
                                       CAIRO_HINT_STYLE_FULL, CAIRO_HINT_METRICS_OFF};
constexpr OptionsValues ChangedOptions{CAIRO_ANTIALIAS_NONE, CAIRO_SUBPIXEL_ORDER_RGB,
                                       CAIRO_HINT_STYLE_NONE, CAIRO_HINT_METRICS_ON};

#if defined(__linux__)

struct NativeCairo {
    void* library{};
    int (*version)(){};
    void* (*toyFontFaceCreate)(const char*, int, int){};
    void (*fontFaceDestroy)(void*){};
    int (*fontFaceStatus)(void*){};
    void* (*fontOptionsCreate)(){};
    void (*fontOptionsDestroy)(void*){};
    void (*fontOptionsSetAntialias)(void*, int){};
    void (*fontOptionsSetSubpixelOrder)(void*, int){};
    void (*fontOptionsSetHintStyle)(void*, int){};
    void (*fontOptionsSetHintMetrics)(void*, int){};
    int (*fontOptionsGetAntialias)(const void*){};
    int (*fontOptionsGetSubpixelOrder)(const void*){};
    int (*fontOptionsGetHintStyle)(const void*){};
    int (*fontOptionsGetHintMetrics)(const void*){};
    void* (*scaledFontCreate)(void*, const CairoMatrix*, const CairoMatrix*, const void*){};
    void* (*scaledFontReference)(void*){};
    void (*scaledFontDestroy)(void*){};
    int (*scaledFontStatus)(void*){};
    void* (*scaledFontGetFontFace)(void*){};
    void (*scaledFontGetFontMatrix)(void*, CairoMatrix*){};
    void (*scaledFontGetCtm)(void*, CairoMatrix*){};
    void (*scaledFontGetFontOptions)(void*, void*){};

    template <typename T>
    T Load(const char* name) {
        auto* symbol = dlsym(library, name);
        Require(symbol != nullptr, name);
        return reinterpret_cast<T>(symbol);
    }

    NativeCairo() {
        library = dlmopen(LM_ID_NEWLM, "libcairo.so.2", RTLD_NOW | RTLD_LOCAL);
        Require(library != nullptr, "native Cairo 1.18.0 could not be loaded");
        version = Load<decltype(version)>("cairo_version");
        toyFontFaceCreate = Load<decltype(toyFontFaceCreate)>("cairo_toy_font_face_create");
        fontFaceDestroy = Load<decltype(fontFaceDestroy)>("cairo_font_face_destroy");
        fontFaceStatus = Load<decltype(fontFaceStatus)>("cairo_font_face_status");
        fontOptionsCreate = Load<decltype(fontOptionsCreate)>("cairo_font_options_create");
        fontOptionsDestroy = Load<decltype(fontOptionsDestroy)>("cairo_font_options_destroy");
        fontOptionsSetAntialias = Load<decltype(fontOptionsSetAntialias)>("cairo_font_options_set_antialias");
        fontOptionsSetSubpixelOrder = Load<decltype(fontOptionsSetSubpixelOrder)>("cairo_font_options_set_subpixel_order");
        fontOptionsSetHintStyle = Load<decltype(fontOptionsSetHintStyle)>("cairo_font_options_set_hint_style");
        fontOptionsSetHintMetrics = Load<decltype(fontOptionsSetHintMetrics)>("cairo_font_options_set_hint_metrics");
        fontOptionsGetAntialias = Load<decltype(fontOptionsGetAntialias)>("cairo_font_options_get_antialias");
        fontOptionsGetSubpixelOrder = Load<decltype(fontOptionsGetSubpixelOrder)>("cairo_font_options_get_subpixel_order");
        fontOptionsGetHintStyle = Load<decltype(fontOptionsGetHintStyle)>("cairo_font_options_get_hint_style");
        fontOptionsGetHintMetrics = Load<decltype(fontOptionsGetHintMetrics)>("cairo_font_options_get_hint_metrics");
        scaledFontCreate = Load<decltype(scaledFontCreate)>("cairo_scaled_font_create");
        scaledFontReference = Load<decltype(scaledFontReference)>("cairo_scaled_font_reference");
        scaledFontDestroy = Load<decltype(scaledFontDestroy)>("cairo_scaled_font_destroy");
        scaledFontStatus = Load<decltype(scaledFontStatus)>("cairo_scaled_font_status");
        scaledFontGetFontFace = Load<decltype(scaledFontGetFontFace)>("cairo_scaled_font_get_font_face");
        scaledFontGetFontMatrix = Load<decltype(scaledFontGetFontMatrix)>("cairo_scaled_font_get_font_matrix");
        scaledFontGetCtm = Load<decltype(scaledFontGetCtm)>("cairo_scaled_font_get_ctm");
        scaledFontGetFontOptions = Load<decltype(scaledFontGetFontOptions)>("cairo_scaled_font_get_font_options");
        Require(version() == 11800, "native Cairo version is not 1.18.0");
    }

    ~NativeCairo() {
        if (library) dlclose(library);
    }

    OptionsValues ReadOptions(const void* options) const {
        return {static_cast<cairo_antialias_t>(fontOptionsGetAntialias(options)),
                static_cast<cairo_subpixel_order_t>(fontOptionsGetSubpixelOrder(options)),
                static_cast<cairo_hint_style_t>(fontOptionsGetHintStyle(options)),
                static_cast<cairo_hint_metrics_t>(fontOptionsGetHintMetrics(options))};
    }

    void SetOptions(void* options, OptionsValues values) const {
        fontOptionsSetAntialias(options, values.antialias);
        fontOptionsSetSubpixelOrder(options, values.subpixelOrder);
        fontOptionsSetHintStyle(options, values.hintStyle);
        fontOptionsSetHintMetrics(options, values.hintMetrics);
    }
};

void TestNativeDifferential(cairo_font_face_t* guestFace, const CairoMatrix& fontMatrix, const CairoMatrix& ctm) {
    NativeCairo native;
    auto* nativeFace = native.toyFontFaceCreate("sans", 0, 0);
    auto* nativeOptions = native.fontOptionsCreate();
    Require(nativeFace != nullptr && nativeOptions != nullptr, "native face or options creation failed");
    native.SetOptions(nativeOptions, InitialOptions);

    auto* nativeScaled = native.scaledFontCreate(nativeFace, &fontMatrix, &ctm, nativeOptions);
    auto* guestOptions = cairo_font_options_create();
    Require(nativeScaled != nullptr && guestOptions != nullptr, "native scaled-font creation failed");
    SetOptions(guestOptions, InitialOptions);
    auto* guestScaled = cairo_scaled_font_create(guestFace, &fontMatrix, &ctm, guestOptions);
    Require(guestScaled != nullptr, "guest scaled-font creation failed");
    Require(native.scaledFontStatus(nativeScaled) == 0 && cairo_scaled_font_status(guestScaled) == 0,
            "native and guest success statuses differ");

    CairoMatrix nativeFontMatrix{};
    CairoMatrix guestFontMatrix{};
    CairoMatrix nativeCtm{};
    CairoMatrix guestCtm{};
    native.scaledFontGetFontMatrix(nativeScaled, &nativeFontMatrix);
    cairo_scaled_font_get_font_matrix(guestScaled, &guestFontMatrix);
    native.scaledFontGetCtm(nativeScaled, &nativeCtm);
    cairo_scaled_font_get_ctm(guestScaled, &guestCtm);
    RequireMatrix(guestFontMatrix, nativeFontMatrix, "font matrix differs from native Cairo");
    RequireMatrix(guestCtm, nativeCtm, "CTM differs from native Cairo");
    RequireMatrix(guestFontMatrix, fontMatrix, "font matrix translation was not preserved");
    RequireMatrix(guestCtm, {ctm.xx, ctm.yx, ctm.xy, ctm.yy, 0, 0}, "CTM translation was not discarded");
    Require(native.scaledFontGetFontFace(nativeScaled) == nativeFace, "native scaled font did not retain its face");
    Require(cairo_scaled_font_get_font_face(guestScaled) == guestFace, "guest scaled font did not retain its face");

    native.SetOptions(nativeOptions, ChangedOptions);
    SetOptions(guestOptions, ChangedOptions);
    auto* nativeResultOptions = native.fontOptionsCreate();
    auto* guestResultOptions = cairo_font_options_create();
    Require(nativeResultOptions != nullptr && guestResultOptions != nullptr, "result options allocation failed");
    native.SetOptions(nativeResultOptions, ChangedOptions);
    SetOptions(guestResultOptions, ChangedOptions);
    native.scaledFontGetFontOptions(nativeScaled, nativeResultOptions);
    cairo_scaled_font_get_font_options(guestScaled, guestResultOptions);
    RequireOptions(ReadOptions(guestResultOptions), native.ReadOptions(nativeResultOptions),
                   "font options differ from native Cairo");
    RequireOptions(ReadOptions(guestResultOptions), InitialOptions, "scaled font did not snapshot its options");

    auto* nativeReference = native.scaledFontReference(nativeScaled);
    auto* guestReference = cairo_scaled_font_reference(guestScaled);
    Require(nativeReference == nativeScaled && guestReference == guestScaled, "reference changed the scaled-font pointer");
    native.fontFaceDestroy(nativeFace);
    Require(native.fontFaceStatus(native.scaledFontGetFontFace(nativeReference)) == 0,
            "native scaled font did not retain its face after caller release");
    native.scaledFontDestroy(nativeScaled);
    cairo_scaled_font_destroy(guestScaled);
    Require(native.scaledFontStatus(nativeReference) == 0 && cairo_scaled_font_status(guestReference) == 0,
            "nonfinal destroy released a scaled font");
    native.scaledFontDestroy(nativeReference);
    cairo_scaled_font_destroy(guestReference);

    native.fontOptionsDestroy(nativeResultOptions);
    cairo_font_options_destroy(guestResultOptions);
    native.fontOptionsDestroy(nativeOptions);
    cairo_font_options_destroy(guestOptions);

    auto* nativeErrorOptions = native.fontOptionsCreate();
    auto* guestErrorOptions = cairo_font_options_create();
    native.SetOptions(nativeErrorOptions, ChangedOptions);
    SetOptions(guestErrorOptions, ChangedOptions);
    auto* nativeErrorFace = native.toyFontFaceCreate("sans", 0, 0);
    auto* nativeNullOptions = native.scaledFontCreate(nativeErrorFace, &fontMatrix, &ctm, nullptr);
    auto* guestNullOptions = cairo_scaled_font_create(guestFace, &fontMatrix, &ctm, nullptr);
    Require(native.scaledFontStatus(nativeNullOptions) == 7 && cairo_scaled_font_status(guestNullOptions) == 7,
            "null options status differs from native Cairo");
    native.scaledFontGetFontMatrix(nativeNullOptions, &nativeFontMatrix);
    cairo_scaled_font_get_font_matrix(guestNullOptions, &guestFontMatrix);
    RequireMatrix(guestFontMatrix, nativeFontMatrix, "null-options error font matrix differs from native Cairo");
    native.scaledFontGetCtm(nativeNullOptions, &nativeCtm);
    cairo_scaled_font_get_ctm(guestNullOptions, &guestCtm);
    RequireMatrix(guestCtm, nativeCtm, "null-options error CTM differs from native Cairo");
    native.scaledFontGetFontOptions(nativeNullOptions, nativeErrorOptions);
    cairo_scaled_font_get_font_options(guestNullOptions, guestErrorOptions);
    RequireOptions(ReadOptions(guestErrorOptions), native.ReadOptions(nativeErrorOptions),
                   "null-options error options differ from native Cairo");
    Require(native.fontFaceStatus(native.scaledFontGetFontFace(nativeNullOptions)) == 1,
            "native error scaled font did not return the NO_MEMORY face");
    Require(cairo_font_face_set_user_data(cairo_scaled_font_get_font_face(guestNullOptions), nullptr, nullptr, nullptr) == 1,
            "guest error scaled font did not return the NO_MEMORY face");
    native.scaledFontDestroy(nativeNullOptions);
    cairo_scaled_font_destroy(guestNullOptions);
    native.fontFaceDestroy(nativeErrorFace);
    native.fontOptionsDestroy(nativeErrorOptions);
    cairo_font_options_destroy(guestErrorOptions);

    auto* nativeInvalidFace = native.toyFontFaceCreate(nullptr, 0, 0);
    auto* guestInvalidFace = cairo_ft_font_face_create_for_ft_face(nullptr, 0);
    auto* nativeFaceErrorOptions = native.fontOptionsCreate();
    auto* guestFaceErrorOptions = cairo_font_options_create();
    Require(native.fontFaceStatus(nativeInvalidFace) == 7 &&
                cairo_font_face_set_user_data(guestInvalidFace, nullptr, nullptr, nullptr) == 7,
            "invalid font-face setup differs from native Cairo");
    auto* nativeInvalidFaceScaled = native.scaledFontCreate(nativeInvalidFace, &fontMatrix, &ctm, nativeFaceErrorOptions);
    auto* guestInvalidFaceScaled =
        cairo_scaled_font_create(guestInvalidFace, &fontMatrix, &ctm, guestFaceErrorOptions);
    Require(native.scaledFontStatus(nativeInvalidFaceScaled) == 7 &&
                cairo_scaled_font_status(guestInvalidFaceScaled) == 7,
            "font-face error status was not propagated");
    Require(native.fontFaceStatus(native.scaledFontGetFontFace(nativeInvalidFaceScaled)) == 1 &&
                cairo_font_face_set_user_data(cairo_scaled_font_get_font_face(guestInvalidFaceScaled), nullptr, nullptr,
                                              nullptr) == 1,
            "font-face error did not return the NO_MEMORY face");
    auto* nativeInvalidFaceReference = native.scaledFontReference(nativeInvalidFaceScaled);
    auto* guestInvalidFaceReference = cairo_scaled_font_reference(guestInvalidFaceScaled);
    Require(nativeInvalidFaceReference == nativeInvalidFaceScaled &&
                guestInvalidFaceReference == guestInvalidFaceScaled,
            "error scaled-font reference changed its pointer");
    native.scaledFontDestroy(nativeInvalidFaceScaled);
    cairo_scaled_font_destroy(guestInvalidFaceScaled);
    native.scaledFontDestroy(nativeInvalidFaceReference);
    cairo_scaled_font_destroy(guestInvalidFaceReference);
    native.fontFaceDestroy(nativeInvalidFace);
    native.fontOptionsDestroy(nativeFaceErrorOptions);
    cairo_font_options_destroy(guestFaceErrorOptions);

    auto* nativeNanFace = native.toyFontFaceCreate("sans", 0, 0);
    auto* nativeNanOptions = native.fontOptionsCreate();
    auto* guestNanOptions = cairo_font_options_create();
    CairoMatrix invalidMatrix{NAN, 0, 0, 1, 0, 0};
    auto* nativeInvalid = native.scaledFontCreate(nativeNanFace, &invalidMatrix, &ctm, nativeNanOptions);
    auto* guestInvalid = cairo_scaled_font_create(guestFace, &invalidMatrix, &ctm, guestNanOptions);
    Require(native.scaledFontStatus(nativeInvalid) == 5 && cairo_scaled_font_status(guestInvalid) == 5,
            "invalid matrix status differs from native Cairo");
    auto* nativeInvalidWithoutOptions = native.scaledFontCreate(nativeNanFace, &invalidMatrix, &ctm, nullptr);
    auto* guestInvalidWithoutOptions = cairo_scaled_font_create(guestFace, &invalidMatrix, &ctm, nullptr);
    Require(native.scaledFontStatus(nativeInvalidWithoutOptions) == 5 &&
                cairo_scaled_font_status(guestInvalidWithoutOptions) == 5,
            "invalid matrix did not take precedence over null options");
    native.scaledFontDestroy(nativeInvalid);
    cairo_scaled_font_destroy(guestInvalid);
    native.scaledFontDestroy(nativeInvalidWithoutOptions);
    cairo_scaled_font_destroy(guestInvalidWithoutOptions);
    native.fontFaceDestroy(nativeNanFace);
    native.fontOptionsDestroy(nativeNanOptions);
    cairo_font_options_destroy(guestNanOptions);

    auto* nativeZeroFace = native.toyFontFaceCreate("sans", 0, 0);
    auto* nativeZeroOptions = native.fontOptionsCreate();
    auto* guestZeroOptions = cairo_font_options_create();
    CairoMatrix zeroMatrix{};
    auto* nativeZero = native.scaledFontCreate(nativeZeroFace, &zeroMatrix, &ctm, nativeZeroOptions);
    auto* guestZero = cairo_scaled_font_create(guestFace, &zeroMatrix, &ctm, guestZeroOptions);
    Require(native.scaledFontStatus(nativeZero) == 0 && cairo_scaled_font_status(guestZero) == 0,
            "singular matrix behavior differs from native Cairo");
    native.scaledFontDestroy(nativeZero);
    cairo_scaled_font_destroy(guestZero);
    native.fontFaceDestroy(nativeZeroFace);
    native.fontOptionsDestroy(nativeZeroOptions);
    cairo_font_options_destroy(guestZeroOptions);

    auto* nativeDefaultFace = native.toyFontFaceCreate("sans", 0, 0);
    auto* nativeDefaultOptions = native.fontOptionsCreate();
    auto* guestDefaultOptions = cairo_font_options_create();
    auto* nativeDefault = native.scaledFontCreate(nativeDefaultFace, &fontMatrix, &ctm, nativeDefaultOptions);
    auto* guestDefault = cairo_scaled_font_create(guestFace, &fontMatrix, &ctm, guestDefaultOptions);
    auto* nativeDefaultOutput = native.fontOptionsCreate();
    auto* guestDefaultOutput = cairo_font_options_create();
    native.SetOptions(nativeDefaultOutput, ChangedOptions);
    SetOptions(guestDefaultOutput, ChangedOptions);
    native.scaledFontGetFontOptions(nativeDefault, nativeDefaultOutput);
    cairo_scaled_font_get_font_options(guestDefault, guestDefaultOutput);
    RequireOptions(ReadOptions(guestDefaultOutput), native.ReadOptions(nativeDefaultOutput),
                   "default font options differ from native Cairo");
    RequireOptions(ReadOptions(guestDefaultOutput), Defaults, "default scaled-font options did not replace output values");
    native.scaledFontDestroy(nativeDefault);
    cairo_scaled_font_destroy(guestDefault);
    native.fontFaceDestroy(nativeDefaultFace);
    native.fontOptionsDestroy(nativeDefaultOptions);
    cairo_font_options_destroy(guestDefaultOptions);
    native.fontOptionsDestroy(nativeDefaultOutput);
    cairo_font_options_destroy(guestDefaultOutput);
    cairo_font_face_destroy(guestFace);
}

#endif

}

int main() {
    FreeType::GuestLibrary* guestLibrary = nullptr;
    Require(FT_Init_FreeType_nid_postfix(&guestLibrary) == 0, "guest FreeType initialization failed");

    auto font = SquareGlyphFont('A', 913);
    auto createFace = [&] {
        FreeType::GuestFaceRec* guestFace = nullptr;
        Require(FT_New_Memory_Face_nid_postfix(guestLibrary, font.data(), static_cast<std::int64_t>(font.size()), 0,
                                               &guestFace) == 0,
                "guest FreeType face creation failed");
        auto* result = cairo_ft_font_face_create_for_ft_face(guestFace, FT_LOAD_DEFAULT);
        Require(result != nullptr && FT_Done_Face_nid_postfix(guestFace) == 0,
                "guest Cairo face retention failed");
        return result;
    };

    auto* face = createFace();

    cairo_user_data_key_t faceKey{1};
    int faceData{};
    Require(cairo_font_face_set_user_data(face, &faceKey, &faceData, RecordFaceDestroy) == 0,
            "font-face finalizer registration failed");

    const CairoMatrix fontMatrix{12, 1, 2, 13, 5, 6};
    const CairoMatrix ctm{2, 0.5, 0.25, 3, 77, 88};
    auto* options = cairo_font_options_create();
    Require(options != nullptr, "font options allocation failed");
    SetOptions(options, InitialOptions);

    auto* scaledFont = cairo_scaled_font_create(face, &fontMatrix, &ctm, options);
    Require(scaledFont != nullptr && cairo_scaled_font_status(scaledFont) == 0, "scaled-font creation failed");
    CairoMatrix actualFontMatrix{};
    CairoMatrix actualCtm{};
    cairo_scaled_font_get_font_matrix(scaledFont, &actualFontMatrix);
    cairo_scaled_font_get_ctm(scaledFont, &actualCtm);
    RequireMatrix(actualFontMatrix, fontMatrix, "font matrix was not copied");
    RequireMatrix(actualCtm, {ctm.xx, ctm.yx, ctm.xy, ctm.yy, 0, 0}, "CTM translation was not normalized");

    SetOptions(options, ChangedOptions);
    auto* resultOptions = cairo_font_options_create();
    Require(resultOptions != nullptr, "result font options allocation failed");
    SetOptions(resultOptions, ChangedOptions);
    cairo_scaled_font_get_font_options(scaledFont, resultOptions);
    RequireOptions(ReadOptions(resultOptions), InitialOptions, "scaled font did not retain its original options");
    cairo_font_options_destroy(resultOptions);
    cairo_font_options_destroy(options);

    auto* reference = cairo_scaled_font_reference(scaledFont);
    Require(reference == scaledFont, "scaled-font reference changed its pointer");
    cairo_font_face_destroy(face);
    Require(faceDestroyCount == 0, "scaled font did not retain its font face");
    Require(cairo_scaled_font_get_font_face(reference) != nullptr, "scaled-font face getter returned null");
    Require(cairo_font_face_get_user_data(cairo_scaled_font_get_font_face(reference), &faceKey) == &faceData,
            "scaled-font face getter did not return the retained face");
    cairo_scaled_font_destroy(scaledFont);
    Require(cairo_scaled_font_status(reference) == 0 && faceDestroyCount == 0,
            "nonfinal scaled-font destroy released its face");
    cairo_scaled_font_destroy(reference);
    Require(faceDestroyCount == 1, "final scaled-font destroy did not release its font face");

#if defined(__linux__)
    TestNativeDifferential(createFace(), fontMatrix, ctm);
#endif

    auto* errorOptions = cairo_font_options_create();
    auto* nullOptions = cairo_scaled_font_create(cairo_ft_font_face_create_for_ft_face(nullptr, 0), &fontMatrix, &ctm,
                                                 errorOptions);
    Require(nullOptions != nullptr && cairo_scaled_font_status(nullOptions) == 7,
            "font-face error status was not propagated");
    Require(cairo_font_face_set_user_data(cairo_scaled_font_get_font_face(nullOptions), &faceKey, &faceData, nullptr) == 1,
            "error scaled font did not return the NO_MEMORY face");
    cairo_scaled_font_destroy(nullOptions);

    auto* nullOptionsScaled = cairo_scaled_font_create(
        cairo_ft_font_face_create_for_ft_face(nullptr, 0), &fontMatrix, &ctm, nullptr);
    Require(cairo_scaled_font_status(nullOptionsScaled) == 7, "null options did not report NULL_POINTER");
    CairoMatrix errorMatrix{};
    cairo_scaled_font_get_font_matrix(nullOptionsScaled, &errorMatrix);
    RequireMatrix(errorMatrix, Identity, "error scaled-font matrix was not identity");
    cairo_scaled_font_get_ctm(nullOptionsScaled, &errorMatrix);
    RequireMatrix(errorMatrix, Identity, "error scaled-font CTM was not identity");
    auto* errorResultOptions = cairo_font_options_create();
    SetOptions(errorResultOptions, ChangedOptions);
    cairo_scaled_font_get_font_options(nullOptionsScaled, errorResultOptions);
    RequireOptions(ReadOptions(errorResultOptions), Defaults, "error scaled-font options were not defaulted");
    Require(cairo_scaled_font_reference(nullOptionsScaled) == nullOptionsScaled,
            "error scaled-font reference changed its pointer");
    cairo_scaled_font_destroy(nullOptionsScaled);
    cairo_scaled_font_destroy(nullOptionsScaled);
    cairo_font_options_destroy(errorResultOptions);
    cairo_font_options_destroy(errorOptions);

    auto* nilOptions = cairo_font_options_copy(nullptr);
    auto* nilOptionsScaled = cairo_scaled_font_create(
        cairo_ft_font_face_create_for_ft_face(nullptr, 0), &fontMatrix, &ctm, nilOptions);
    Require(cairo_scaled_font_status(nilOptionsScaled) == 7,
            "font-face status precedence differs from Cairo error propagation");
    cairo_scaled_font_destroy(nilOptionsScaled);
    cairo_font_options_destroy(nilOptions);

    auto* validFace = createFace();
    auto* validOptions = cairo_font_options_create();
    auto* nilOptionsOnly = cairo_font_options_copy(nullptr);
    auto* nilOptionsError = cairo_scaled_font_create(validFace, &fontMatrix, &ctm, nilOptionsOnly);
    Require(cairo_scaled_font_status(nilOptionsError) == 1, "nil options did not report NO_MEMORY");
    cairo_scaled_font_destroy(nilOptionsError);
    auto* nullOptionsError = cairo_scaled_font_create(validFace, &fontMatrix, &ctm, nullptr);
    Require(cairo_scaled_font_status(nullOptionsError) == 7, "null options did not report NULL_POINTER");
    cairo_scaled_font_destroy(nullOptionsError);
    CairoMatrix nanMatrix{NAN, 0, 0, 1, 0, 0};
    auto* invalidMatrix = cairo_scaled_font_create(validFace, &nanMatrix, &ctm, validOptions);
    Require(cairo_scaled_font_status(invalidMatrix) == 5, "invalid matrix did not report INVALID_MATRIX");
    cairo_scaled_font_destroy(invalidMatrix);
    auto* nullFontMatrix = cairo_scaled_font_create(validFace, nullptr, &ctm, validOptions);
    Require(cairo_scaled_font_status(nullFontMatrix) == 7, "null font matrix did not report NULL_POINTER");
    cairo_scaled_font_destroy(nullFontMatrix);
    auto* nullCtm = cairo_scaled_font_create(validFace, &fontMatrix, nullptr, validOptions);
    Require(cairo_scaled_font_status(nullCtm) == 7, "null CTM did not report NULL_POINTER");
    cairo_scaled_font_destroy(nullCtm);
    CairoMatrix zeroMatrix{};
    auto* singular = cairo_scaled_font_create(validFace, &zeroMatrix, &ctm, validOptions);
    Require(cairo_scaled_font_status(singular) == 0, "singular font matrix was rejected");
    cairo_scaled_font_destroy(singular);
    cairo_font_face_destroy(validFace);
    cairo_font_options_destroy(nilOptionsOnly);
    cairo_font_options_destroy(validOptions);

    Require(cairo_scaled_font_status(nullptr) == 7, "null scaled-font status differs from Cairo");
    cairo_scaled_font_destroy(nullptr);

    Require(FT_Done_FreeType_nid_postfix(guestLibrary) == 0, "guest FreeType shutdown failed");
    std::puts("cairo scaled-font tests passed");
    return 0;
}
