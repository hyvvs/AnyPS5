# libc

## add Narrow secure scanf field widths

- [Narrow secure scanf field widths](../../../core/libs/prx/libc/src/Formatting.cpp) (libc) reject decimal widths above `INT_MAX` with `std::out_of_range` before accessing an output buffer. Host scanning uses a signed `int` width; previously a width of `18446744073709551616` wrapped to zero, making secure `%c` scan one character into a one-byte output and removing a `%s` width limit. Ordinary scanf retains its established treatment of oversized `%c` widths. The console's behavior with oversized widths has not been measured.
