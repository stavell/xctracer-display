#pragma once
// Per-file -O3 for the measured hot loops (device only; the emulator keeps its own flags so the tests see the same code
// they always did). Put `HOT_FILE_O3` as the first line of a .cpp, after this include, so inlined header code gets it too.
#if defined(__arm__) && defined(__GNUC__) && !defined(__clang__)
#define HOT_FILE_O3 _Pragma("GCC optimize(\"O3\")")
#else
#define HOT_FILE_O3
#endif
