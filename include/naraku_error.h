/**
 * @file naraku_error.h
 */

#ifndef NARAKU_ERROR_H
#define NARAKU_ERROR_H

#include <naraku_common.h>

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
  // Normal cases (0..-99):
  //
  // ============================================================================

  NK_SUCCESS = 0,
  NK_NO_MATCH = -1,

  // ============================================================================
  //
  // General errors (-100..-199):
  //
  // ============================================================================

  NK_ERR_MEMORY_ALLOCATION_FAILED = -100,

  // ============================================================================
  //
  // Internal errors (-200..-299):
  //
  // ============================================================================

  NK_ERR_INTERNAL_ERROR = -200,
  NK_ERR_PARSER_BUG = -201,

  // ============================================================================
  //
  // Parser-related errors (-300..-499):
  //
  // ============================================================================

  // Errors on reading bytes from the pattern:
  NK_ERR_INVALID_BYTE_SEQUENCE = -300,
  NK_ERR_INCOMPLETE_BYTE_SEQUENCE = -301,

  // Errors related to general escape:
  NK_ERR_INCOMPLETE_ESCAPE = -310,
  NK_ERR_INVALID_ESCAPE = -311,

  // Errors related to byte escape sequence (e.g., `\xHH`, `\OOO`, `\cX`, `\M-X`):
  NK_ERR_INVALID_ESCAPED_BYTE_SEQUENCE = -320,
  NK_ERR_INCOMPLETE_ESCAPED_BYTE_SEQUENCE = -321,
  NK_ERR_INCOMPLETE_HEX_ESCAPE = -322,
  NK_ERR_INCOMPLETE_META_ESCAPE = -323,
  NK_ERR_INCOMPLETE_CONTROL_ESCAPE = -324,
  NK_ERR_INVALID_META_ESCAPE_CODE = -325,
  NK_ERR_INVALID_CONTROL_ESCAPE_CODE = -326,
  NK_ERR_DUPLICATE_META_ESCAPE = -327,
  NK_ERR_DUPLICATE_CONTROL_ESCAPE = -328,

  // Errors related to Unicode code point escape sequence (`\uHHHH` and `\u{...}`):
  NK_ERR_INCOMPLETE_UNICODE_ESCAPE = -330,
  NK_ERR_EMPTY_UNICODE_ESCAPE_BRACE = -331,
  NK_ERR_INVALID_UNICODE_ESCAPE = -332,
  NK_ERR_UNCLOSED_UNICODE_ESCAPE_BRACE = -333,
  NK_ERR_UNICODE_ESCAPE_IN_NON_UNICODE_ENCODING = -334,

  // Errors related to character properties (e.g., `\p{Lu}`, `\P{Lu}`):
  NK_ERR_UNCLOSED_CHAR_PROP_ESCAPE_BRACE = -341,

  // Errors related to bounded quantifiers (e.g., `{m,n}`):
  NK_ERR_TOO_BIG_NUMBER_IN_QUANTIFIER = -400,
  NK_ERR_NUMBERS_OUT_OF_ORDER_IN_QUANTIFIER = -401,

  // ============================================================================
  //
  // Encoding-related errors (-500..-599):
  //
  // ============================================================================

  // Errors related to code points:
  NK_ERR_INVALID_CODE_POINT = -500,
  NK_ERR_TOO_LARGE_CODE_POINT = -502,

  // Errors related to character properties:
  NK_ERR_INVALID_CHAR_PROP_NAME = -513,
  NK_ERR_UNSUPPORTED_CHAR_PROPERTY = -514,
} nk_error_t;

// ==========================================================================
//
// src/error.c
//
// ==========================================================================

/**
 * Returns a human-readable error message corresponding to the given error code `err`.
 */
NARAKU_EXPORTED_FUNCTION
const uint8_t* nk_error_message(nk_error_t err);

#ifdef __cplusplus
}
#endif

#endif  // NARAKU_ERROR_H
