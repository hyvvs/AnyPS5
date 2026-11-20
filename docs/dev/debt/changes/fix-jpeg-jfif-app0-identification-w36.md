# libSceJpegDec

## add Functional

- [sceJpegDecParseHeader and sceJpegDecDecode](../../../core/libs/prx/libSceJpegDec/Export.cpp) only infer YCbCr from an APP0 segment when its five-byte identifier is `JFIF\0`, rather than from any APP0 marker. An unrelated APP0 does not override nonstandard component IDs. This follows the [Library of Congress JFIF format description](https://www.loc.gov/preservation/digital/formats/fdd/fdd000619.shtml); Sony's color-space classification for non-JFIF APP0 segments remains unmeasured.
