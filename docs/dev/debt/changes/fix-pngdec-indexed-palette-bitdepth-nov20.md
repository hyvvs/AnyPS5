# libScePngDec

## add Functional

- [scePngDecParseHeader and scePngDecDecode](../../../core/libs/prx/libScePngDec/Export.cpp) reject indexed-colour `PLTE` chunks whose RGB entry count exceeds `2^bit_depth` from `IHDR`, as required by the [PNG palette specification](https://www.w3.org/TR/png/#11PLTE). This supplements the 1–256-entry and chunk-order checks while retaining valid one-bit, two-bit and eight-bit indexed palettes and optional truecolour palettes. The emulator returns its existing `0x80690010` invalid-data status, but exact PS5 error precedence for malformed index palettes has not been measured.
