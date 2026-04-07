/**
 * @file naraku_error.h
 */

#ifndef NARAKU_ERROR_H
#define NARAKU_ERROR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Type representing an error code.
 * 
 * The error codes are negative integers.
 */
typedef enum {
  // ============================================================================
  //
  // Normal cases:
  //
  // ============================================================================

  /**
   * Indicates success (i.e., no error).
   */
  NK_SUCCESS = 0,

  /**
   * Indicates that a search operation did not find a match.
   */
  NK_NO_MATCH = -1,

  // ============================================================================
  //
  // General and internal errors:
  //
  // ============================================================================

  /**
   * Indicates that a memory allocation failed.
   */
  NK_ERR_MEMORY_ALLOCATION_FAILED = -5,

  /**
   * Indicates an internal error.
   */
  NK_ERR_INTERNAL_ERROR = -7,

  // ============================================================================
  //
  // Character property related errors:
  //
  // ============================================================================

  /**
   * Indicates an invalid character property name.
   */
  NK_ERR_INVALID_CHAR_PROP_NAME = -223,

  /**
   * Indicates an unsupported character property.
   */
  NK_ERR_UNSUPPORTED_CHAR_PROPERTY = -224,

  // ============================================================================
  //
  // Code-points related errors:
  //
  // ============================================================================

  /**
   * Indicates an invalid code point.
   */
  NK_ERR_INVALID_CODE_POINT = -400,

  /**
   * Indicates a code point that is too large to be encoded in the encoding
   * (e.g. above U+10FFFF for UTF-8).
   */
  NK_ERR_TOO_LARGE_CODE_POINT = -401,
} nk_error_t;

#ifdef __cplusplus
}
#endif

#endif // NARAKU_ERROR_H
