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

#if !defined(NARAKU_ARG_UNUSED)
#if defined(__GNUC__) || defined(__clang__)
#define NARAKU_ARG_UNUSED __attribute__((unused))
#else
#define NARAKU_ARG_UNUSED
#endif
#endif

#if !defined(NARAKU_FALLTHROUGH)
#if defined(__GNUC__) || defined(__clang__)
#define NARAKU_FALLTHROUGH __attribute__((fallthrough))
#else
#define NARAKU_FALLTHROUGH
#endif
#endif

#endif  // NARAKU_COMMON_H
