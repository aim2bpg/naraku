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
typedef enum nk_error {
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
  NK_ERR_UNEXPECTED_END_OF_PATTERN = -302,

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
  NK_ERR_EMPTY_CHAR_PROP_NAME = -342,

  // Errors related to quantifiers (e.g., `*`, `{m,n}`):
  NK_ERR_TOO_LARGE_NUMBER_IN_QUANTIFIER = -350,
  NK_ERR_NUMBERS_OUT_OF_ORDER_IN_QUANTIFIER = -351,
  NK_ERR_NOTHING_TO_REPEAT = -352,

  // Errors related to groups:
  NK_ERR_UNMATCHED_CLOSE_PARENTHESIS = -360,
  NK_ERR_INCOMPLETE_GROUP_SPECIFIER = -361,
  NK_ERR_UNDEFINED_GROUP_OPTION = -362,
  NK_ERR_UNTERMINATED_GROUP = -363,
  NK_ERR_TOO_MANY_CAPTURE_GROUPS = -364,
  NK_ERR_INVALID_CONDITIONAL_GROUP = -365,
  NK_ERR_INVALID_CONDITIONAL_GROUP_NUMBER = -366,
  NK_ERR_PARSE_DEPTH_LIMIT_EXCEEDED = -367,

  // Errors related to group names and group numbers
  // (for named groups, named back-references, and sub-expression calls):
  NK_ERR_INVALID_GROUP_NAME = -370,
  NK_ERR_EMPTY_GROUP_NAME = -371,
  NK_ERR_TOO_LARGE_GROUP_NUMBER = -372,
  NK_ERR_GROUP_NUMBER_OUT_OF_RANGE = -373,
  NK_ERR_INCOMPLETE_CAPTURE_DEPTH = -374,
  NK_ERR_TOO_LARGE_CAPTURE_DEPTH = -375,

  // Errors related to sub-expression calls:
  NK_ERR_INCOMPLETE_SUBEXP_CALL = -380,

  // Errors related to back-references:
  NK_ERR_INCOMPLETE_BACK_REF = -390,
  NK_ERR_INVALID_BACK_REF = -391,

  // Errors related to character classes:
  NK_ERR_UNTERMINATED_CHAR_CLASS = -400,
  NK_ERR_INVALID_CHAR_CLASS_RANGE = -401,
  NK_ERR_CHAR_CLASS_RANGE_OUT_OF_ORDER = -402,
  NK_ERR_EMPTY_CHAR_CLASS = -403,

  // Errors related to POSIX character classes (e.g., `[[:digit:]]`):
  NK_ERR_EMPTY_POSIX_CHAR_CLASS_NAME = -410,
  NK_ERR_INVALID_POSIX_CHAR_CLASS_NAME = -411,

  // ============================================================================
  //
  // Encoding-related errors (-500..-599):
  //
  // ============================================================================

  // Errors related to code points:
  NK_ERR_INVALID_CODE_POINT = -500,
  NK_ERR_CODE_POINT_OUT_OF_RANGE = -501,

  // Errors related to character properties:
  NK_ERR_INVALID_CHAR_PROP_NAME = -510,
  NK_ERR_UNSUPPORTED_CHAR_PROPERTY = -511,
} nk_error_t;

/**
 * Type representing a warning code.
 */
typedef enum nk_warning {
  NK_WARN_INCOMPLETE_CHAR_PROP_ESCAPE,
  NK_WARN_INCOMPLETE_NAMED_BACK_REF_ESCAPE,
  NK_WARN_INCOMPLETE_SUBEXP_CALL_ESCAPE,
  NK_WARN_LITERAL_RIGHT_BRACKET_OUTSIDE_CHAR_CLASS,
  NK_WARN_LITERAL_HYPHEN_IN_CHAR_CLASS,
  NK_WARN_LITERAL_RIGHT_BRACKET_IN_CHAR_CLASS,
  NK_WARN_LITERAL_HYPHEN_AT_BEGINNING_OF_CHAR_CLASS,
  NK_WARN_LITERAL_HYPHEN_AT_END_OF_CHAR_CLASS,
} nk_warning_t;

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

/**
 * Returns a human-readable warning message corresponding to the given warning code `warning`.
 */
NARAKU_EXPORTED_FUNCTION
const uint8_t* nk_warning_message(nk_warning_t warning);

#ifdef __cplusplus
}
#endif

#endif  // NARAKU_ERROR_H
