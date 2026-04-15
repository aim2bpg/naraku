#ifndef NARAKU_COMMON_H
#define NARAKU_COMMON_H

#if !defined(NARAKU_EXPORTED_FUNCTION) && !defined(NARAKU_EXPORTED_DATA)
#ifdef NARAKU_EXPORT_SYMBOLS
#ifdef _WIN32
#define NARAKU_EXPORTED_FUNCTION __declspec(dllexport) extern
#define NARAKU_EXPORTED_DATA __declspec(dllexport) extern
#else
#define NARAKU_EXPORTED_FUNCTION __attribute__((__visibility__("default"))) extern
#define NARAKU_EXPORTED_DATA __attribute__((__visibility__("default"))) extern
#endif
#else
#define NARAKU_EXPORTED_FUNCTION
#define NARAKU_EXPORTED_DATA
#endif
#endif

#endif  // NARAKU_COMMON_H
