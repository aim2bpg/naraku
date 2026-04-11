#include <naraku_error.h>

#include <stddef.h>

const uint8_t* nk_error_message(nk_error_t err) {
  switch (err) {
    // Normal cases:
    case NK_SUCCESS:
      return NULL;
    case NK_NO_MATCH:
      return (const uint8_t*)"no match";
    // General errors:
    case NK_ERR_MEMORY_ALLOCATION_FAILED:
      return (const uint8_t*)"memory allocation failed";
    // Internal errors:
    case NK_ERR_INTERNAL_ERROR:
      return (const uint8_t*)"BUG: internal error";
    case NK_ERR_PARSER_BUG:
      return (const uint8_t*)"BUG: parser";
    // Parser-related errors:
    case NK_ERR_INVALID_BYTE_SEQUENCE:
      return (const uint8_t*)"invalid byte sequence";
    case NK_ERR_INCOMPLETE_BYTE_SEQUENCE:
      return (const uint8_t*)"incomplete byte sequence";
    case NK_ERR_INCOMPLETE_ESCAPE:
      return (const uint8_t*)"incomplete escape sequence";
    case NK_ERR_INVALID_ESCAPE:
      return (const uint8_t*)"invalid escape sequence";
    case NK_ERR_INVALID_ESCAPED_BYTE_SEQUENCE:
      return (const uint8_t*)"invalid escaped byte sequence";
    case NK_ERR_INCOMPLETE_ESCAPED_BYTE_SEQUENCE:
      return (const uint8_t*)"incomplete escaped byte sequence";
    case NK_ERR_INCOMPLETE_HEX_ESCAPE:
      return (const uint8_t*)"incomplete \\x escape sequence";
    case NK_ERR_INCOMPLETE_META_ESCAPE:
      return (const uint8_t*)"incomplete \\M- escape sequence";
    case NK_ERR_INCOMPLETE_CONTROL_ESCAPE:
      return (const uint8_t*)"incomplete \\c/\\C- escape sequence";
    case NK_ERR_INVALID_META_ESCAPE_CODE:
      return (const uint8_t*)"invalid code in \\M- escape sequence";
    case NK_ERR_INVALID_CONTROL_ESCAPE_CODE:
      return (const uint8_t*)"invalid code in \\c/\\C- escape sequence";
    case NK_ERR_DUPLICATE_META_ESCAPE:
      return (const uint8_t*)"duplicate \\M- escape sequence";
    case NK_ERR_DUPLICATE_CONTROL_ESCAPE:
      return (const uint8_t*)"duplicate \\c/\\C- escape sequence";
    case NK_ERR_INCOMPLETE_UNICODE_ESCAPE:
      return (const uint8_t*)"incomplete Unicode escape sequence";
    case NK_ERR_EMPTY_UNICODE_ESCAPE_BRACE:
      return (const uint8_t*)"empty Unicode escape sequence brace";
    case NK_ERR_INVALID_UNICODE_ESCAPE:
      return (const uint8_t*)"invalid Unicode escape sequence";
    case NK_ERR_UNCLOSED_UNICODE_ESCAPE_BRACE:
      return (const uint8_t*)"unclosed Unicode escape sequence brace";
    case NK_ERR_UNICODE_ESCAPE_IN_NON_UNICODE_ENCODING:
      return (const uint8_t*)"Unicode escape sequence in non-Unicode encoding";
    case NK_ERR_UNCLOSED_CHAR_PROP_ESCAPE_BRACE:
      return (const uint8_t*)"unclosed character property escape sequence brace";
    case NK_ERR_TOO_BIG_NUMBER_IN_QUANTIFIER:
      return (const uint8_t*)"number in quantifier is too big";
    case NK_ERR_NUMBERS_OUT_OF_ORDER_IN_QUANTIFIER:
      return (const uint8_t*)"numbers in quantifier are out of order";
    case NK_ERR_UNMATCHED_CLOSE_PARENTHESIS:
      return (const uint8_t*)"unmatched close parenthesis";
    // Encoding-related errors:
    case NK_ERR_INVALID_CODE_POINT:
      return (const uint8_t*)"invalid code point";
    case NK_ERR_TOO_LARGE_CODE_POINT:
      return (const uint8_t*)"code point is too large";
    case NK_ERR_INVALID_CHAR_PROP_NAME:
      return (const uint8_t*)"invalid character property name";
    case NK_ERR_UNSUPPORTED_CHAR_PROPERTY:
      return (const uint8_t*)"unsupported character property";
  }

  return (const uint8_t*)"BUG: unknown error";
}
