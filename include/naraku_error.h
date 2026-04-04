/**
 * @file naraku_error.h
 */

#ifndef NARAKU_ERROR_H
#define NARAKU_ERROR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t nk_error_t;

// ============================================================================
//
// General and internal errors:
//
// ============================================================================

#define NK_ERR_INTERNAL_ERROR (-7)
#define NK_ERR_MEMORY_ALLOCATION_FAILED (-5)

// ============================================================================
//
// Character property related errors:
//
// ============================================================================

#define NK_ERR_INVALID_CHAR_PROPERTY_NAME (-223)
#define NK_ERR_UNSUPPORTED_CHAR_PROPERTY (-224)

// ============================================================================
//
// Code-points related errors:
//
// ============================================================================

#define NK_ERR_INVALID_CODE_POINT (-400)

#define NK_ERR_TOO_LARGE_CODE_POINT (-401)

// TODO(makenowjust): Do we need to dinstinguish between them? In the most cases,
// `NK_ERR_INVALID_CODE_POINT` is sufficient and `NK_ERR_TOO_LARGE_CODE_POINT`
// is just a special case of `NK_ERR_INVALID_CODE_POINT`.

#ifdef __cplusplus
}
#endif

#endif // NARAKU_ERROR_H
