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
    case NK_ERR_UNEXPECTED_END_OF_PATTERN:
      return (const uint8_t*)"unexpected end of pattern";
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
    case NK_ERR_EMPTY_CHAR_PROP_NAME:
      return (const uint8_t*)"empty character property name";
    case NK_ERR_TOO_LARGE_NUMBER_IN_QUANTIFIER:
      return (const uint8_t*)"number in quantifier is too large";
    case NK_ERR_NUMBERS_OUT_OF_ORDER_IN_QUANTIFIER:
      return (const uint8_t*)"numbers in quantifier are out of order";
    case NK_ERR_NOTHING_TO_REPEAT:
      return (const uint8_t*)"nothing to repeat";
    case NK_ERR_UNMATCHED_CLOSE_PARENTHESIS:
      return (const uint8_t*)"unmatched close parenthesis";
    case NK_ERR_INCOMPLETE_GROUP_SPECIFIER:
      return (const uint8_t*)"incomplete group specifier";
    case NK_ERR_UNDEFINED_GROUP_OPTION:
      return (const uint8_t*)"undefined group option";
    case NK_ERR_UNTERMINATED_GROUP:
      return (const uint8_t*)"unterminated group: missing closing parenthesis";
    case NK_ERR_TOO_MANY_CAPTURE_GROUPS:
      return (const uint8_t*)"too many capture groups";
    case NK_ERR_INVALID_CONDITIONAL_GROUP:
      return (const uint8_t*)"invalid conditional group";
    case NK_ERR_INVALID_CONDITIONAL_GROUP_NUMBER:
      return (const uint8_t*)"invalid conditional group number";
    case NK_ERR_PARSE_DEPTH_LIMIT_EXCEEDED:
      return (const uint8_t*)"parse depth limit exceeded";
    case NK_ERR_INVALID_GROUP_NAME:
      return (const uint8_t*)"invalid group name";
    case NK_ERR_EMPTY_GROUP_NAME:
      return (const uint8_t*)"empty group name";
    case NK_ERR_TOO_LARGE_GROUP_NUMBER:
      return (const uint8_t*)"group number is too large";
    case NK_ERR_GROUP_NUMBER_OUT_OF_RANGE:
      return (const uint8_t*)"group number is out of range";
    case NK_ERR_INCOMPLETE_CAPTURE_DEPTH:
      return (const uint8_t*)"incomplete capture depth";
    case NK_ERR_TOO_LARGE_CAPTURE_DEPTH:
      return (const uint8_t*)"capture depth is too large";
    case NK_ERR_INCOMPLETE_SUBEXP_CALL:
      return (const uint8_t*)"incomplete sub-expression call";
    case NK_ERR_INCOMPLETE_BACK_REF:
      return (const uint8_t*)"incomplete back reference";
    case NK_ERR_INVALID_BACK_REF:
      return (const uint8_t*)"invalid back reference";
    case NK_ERR_UNTERMINATED_CHAR_CLASS:
      return (const uint8_t*)"unterminated character class";
    case NK_ERR_INVALID_CHAR_CLASS_RANGE:
      return (const uint8_t*)"invalid character class range";
    case NK_ERR_CHAR_CLASS_RANGE_OUT_OF_ORDER:
      return (const uint8_t*)"character class range out of order";
    case NK_ERR_EMPTY_CHAR_CLASS:
      return (const uint8_t*)"empty character class";
    case NK_ERR_EMPTY_POSIX_CHAR_CLASS_NAME:
      return (const uint8_t*)"empty POSIX character class name";
    case NK_ERR_INVALID_POSIX_CHAR_CLASS_NAME:
      return (const uint8_t*)"invalid POSIX character class name";
    // Encoding-related errors:
    case NK_ERR_INVALID_CODE_POINT:
      return (const uint8_t*)"invalid code point";
    case NK_ERR_CODE_POINT_OUT_OF_RANGE:
      return (const uint8_t*)"code point is out of range";
    case NK_ERR_INVALID_CHAR_PROP_NAME:
      return (const uint8_t*)"invalid character property name";
    case NK_ERR_UNSUPPORTED_CHAR_PROPERTY:
      return (const uint8_t*)"unsupported character property";
  }

  return (const uint8_t*)"BUG: unknown error";
}

const uint8_t* nk_warning_message(nk_warning_t warning) {
  switch (warning) {
    case NK_WARN_INCOMPLETE_CHAR_PROP_ESCAPE:
      return (const uint8_t*)"incomplete character property escape";
    case NK_WARN_INCOMPLETE_NAMED_BACK_REF_ESCAPE:
      return (const uint8_t*)"incomplete named back-reference escape";
    case NK_WARN_INCOMPLETE_SUBEXP_CALL_ESCAPE:
      return (const uint8_t*)"incomplete sub-expression call escape";
    case NK_WARN_LITERAL_RIGHT_BRACKET_OUTSIDE_CHAR_CLASS:
      return (const uint8_t*)"literal `]` outside character class";
    case NK_WARN_LITERAL_HYPHEN_IN_CHAR_CLASS:
      return (const uint8_t*)"literal `-` in character class";
    case NK_WARN_LITERAL_RIGHT_BRACKET_IN_CHAR_CLASS:
      return (const uint8_t*)"literal `]` in character class";
    case NK_WARN_LITERAL_HYPHEN_AT_BEGINNING_OF_CHAR_CLASS:
      return (const uint8_t*)"literal `-` at beginning of character class";
    case NK_WARN_LITERAL_HYPHEN_AT_END_OF_CHAR_CLASS:
      return (const uint8_t*)"literal `-` at end of character class";
  }

  return (const uint8_t*)"BUG: unknown warning";
}
