#include <naraku_syntax.h>
#include <naraku_syntax_internal.h>

#include <stdlib.h>  // for malloc, free
#include <string.h>  // for memcpy

#include <stdio.h>

#if defined(__GNUC__)
#define ARG_UNUSED __attribute__((unused))
#define FALLTHROUGH __attribute__((fallthrough))
#else
#define ARG_UNUSED
#define FALLTHROUGH
#endif

nk_error_t nk_parser_init(
  const nk_encoding_t* enc,
  const uint8_t* pattern_bytes,
  const uint8_t* pattern_bytes_end,
  nk_parser_options_t options,
  nk_parser_t* out_parser
) {
  out_parser->enc = enc;
  out_parser->pattern_bytes_begin = pattern_bytes;
  out_parser->pattern_bytes_end = pattern_bytes_end;
  out_parser->warning_func = options.warning_func;
  out_parser->user_data = options.user_data;

  out_parser->range_quantifier_max_repetition = options.range_quantifier_max_repetition;
  out_parser->bare_back_ref_max_num = options.bare_back_ref_max_num;
  out_parser->max_group_num = options.max_group_num;
  out_parser->back_ref_max_num = options.back_ref_max_num;
  out_parser->max_capture_depth = options.max_capture_depth;
  out_parser->max_parse_depth = options.max_parse_depth;

  out_parser->pattern_bytes = pattern_bytes;

  out_parser->is_extended_mode = options.is_extended_mode;
  out_parser->is_ignore_case = options.is_ignore_case;
  out_parser->dot_allows_newline = options.dot_allows_newline;
  out_parser->char_class_is_strict = options.char_class_is_strict;
  out_parser->char_type_is_ascii_only = options.char_type_is_ascii_only;
  out_parser->posix_char_class_is_ascii_only = options.posix_char_class_is_ascii_only;
  out_parser->fold_flags = options.fold_flags;

  out_parser->in_unicode_escape_brace = false;
  out_parser->num_capture_groups = 0;
  out_parser->parse_depth = 0;
  out_parser->has_named_groups = false;

  out_parser->error_bytes = NULL;
  out_parser->error_bytes_end = NULL;

  return NK_SUCCESS;
}

void nk_parser_free(nk_parser_t* parser ARG_UNUSED) {
}

// ==========================================================================
//
// Lexer implementation:
//
// ==========================================================================

static inline bool is_decimal_digit(uint32_t code) {
  return '0' <= code && code <= '9';
}

static inline bool is_hexdecimal_digit(uint32_t code) {
  return ('0' <= code && code <= '9') || ('A' <= code && code <= 'F') || ('a' <= code && code <= 'f');
}

static inline bool is_octal_digit(uint32_t code) {
  return '0' <= code && code <= '7';
}

static inline bool is_ascii_printable(uint32_t code) {
  return ('\t' <= code && code <= '\r') || (' ' <= code && code <= '~');
}

/**
 * Peeks at the next Unicode code point in the pattern buffer.
 *
 * This function defensively checks for end-of-pattern, but callers should still
 * check bounds and return context-specific errors when needed.
 */
static inline nk_error_t peek(nk_parser_t* parser, int8_t* out_width, uint32_t* out_code) {
  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    return NK_ERR_UNEXPECTED_END_OF_PATTERN;
  }

  int8_t width = nk_enc_scan_mbc_width(parser->enc, parser->pattern_bytes, parser->pattern_bytes_end);
  if (width == 0) {
    return NK_ERR_INVALID_BYTE_SEQUENCE;
  }
  if (width < 0) {
    return NK_ERR_INCOMPLETE_BYTE_SEQUENCE;
  }

  *out_width = width;
  *out_code = nk_enc_decode_mbc(parser->enc, parser->pattern_bytes, parser->pattern_bytes_end);
  return NK_SUCCESS;
}

/**
 * Consumes the next Unicode code point in the pattern buffer.
 *
 * Note that this function assumes that the parser does not reach the end of
 * the pattern bytes. The caller should check that
 * `parser->pattern_bytes < parser->pattern_bytes_end` before calling this function.
 */
static inline nk_error_t consume(nk_parser_t* parser, int8_t* out_width, uint32_t* out_code) {
  nk_error_t err = peek(parser, out_width, out_code);
  if (err != NK_SUCCESS) {
    return err;
  }

  parser->pattern_bytes += *out_width;
  return NK_SUCCESS;
}

static inline void set_error_span(nk_parser_t* parser, const uint8_t* error_bytes, const uint8_t* error_bytes_end) {
  parser->error_bytes = error_bytes;
  parser->error_bytes_end = error_bytes_end;
}

static inline void set_error_span_to_current(nk_parser_t* parser, const uint8_t* error_bytes) {
  set_error_span(parser, error_bytes, parser->pattern_bytes);
}

static inline void report_warning(
  nk_parser_t* parser,
  nk_warning_t warning,
  const uint8_t* warning_bytes,
  const uint8_t* warning_bytes_end
) {
  if (parser->warning_func == NULL) {
    return;
  }

  parser->warning_func(
    parser,
    warning,
    (size_t)(warning_bytes - parser->pattern_bytes_begin),
    (size_t)(warning_bytes_end - warning_bytes)
  );
}

static inline void consume_contiguous_hex_digits(nk_parser_t* parser) {
  while (parser->pattern_bytes < parser->pattern_bytes_end) {
    int8_t width;
    uint32_t code;
    nk_error_t err = peek(parser, &width, &code);
    if (err != NK_SUCCESS || !is_hexdecimal_digit(code)) {
      return;
    }
    parser->pattern_bytes += width;
  }
}

static nk_error_t
lex_decimal_number(nk_parser_t* parser, uint32_t* out_value, uint32_t max_value, nk_error_t overflow_error) {
  *out_value = 0;
  const uint8_t* num_begin = parser->pattern_bytes;

  while (parser->pattern_bytes < parser->pattern_bytes_end) {
    int8_t width;
    uint32_t code;
    nk_error_t err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (!is_decimal_digit(code)) {
      break;
    }

    uint32_t digit_value = code - '0';

    if (*out_value > max_value / 10 || (*out_value == max_value / 10 && digit_value > max_value % 10)) {
      if (overflow_error != NK_ERR_INTERNAL_ERROR) {
        parser->error_bytes = num_begin;
        parser->error_bytes_end = parser->pattern_bytes + width;
      }
      return overflow_error;
    }

    *out_value = *out_value * 10 + digit_value;
    parser->pattern_bytes += width;
  }

  return NK_SUCCESS;
}

static nk_error_t lex_bounded_quantifier(
  nk_parser_t* parser,
  const uint8_t* quantifier_begin,
  uint32_t* out_min,
  uint32_t* out_max,
  bool* out_is_incomplete,
  bool* out_allows_reluctant
) {
  *out_min = *out_max = 0;
  *out_is_incomplete = *out_allows_reluctant = true;

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    return NK_SUCCESS;  // incomplete quantifier
  }

  int8_t width;
  uint32_t code;
  nk_error_t err = peek(parser, &width, &code);
  if (err != NK_SUCCESS) {
    return err;
  }

  bool has_explicit_min = false;
  if ('0' <= code && code <= '9') {
    nk_error_t err = lex_decimal_number(
      parser,
      out_min,
      parser->range_quantifier_max_repetition,
      NK_ERR_TOO_LARGE_NUMBER_IN_QUANTIFIER
    );
    if (err != NK_SUCCESS) {
      return err;
    }
    has_explicit_min = true;

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      return NK_SUCCESS;  // incomplete quantifier
    }

    err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }
  } else if (code == ',') {
    *out_min = 0;
  } else {
    return NK_SUCCESS;  // incomplete quantifier
  }

  if (code == ',') {                 // `{n,}` or `{n,m}`
    parser->pattern_bytes += width;  // consume `,`

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      return NK_SUCCESS;  // incomplete quantifier
    }

    nk_error_t err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }

    if ('0' <= code && code <= '9') {
      nk_error_t err = lex_decimal_number(
        parser,
        out_max,
        parser->range_quantifier_max_repetition,
        NK_ERR_TOO_LARGE_NUMBER_IN_QUANTIFIER
      );
      if (err != NK_SUCCESS) {
        return err;
      }

      if (parser->pattern_bytes >= parser->pattern_bytes_end) {
        return NK_SUCCESS;  // incomplete quantifier
      }

      err = peek(parser, &width, &code);
      if (err != NK_SUCCESS) {
        return err;
      }
    } else {
      if (!has_explicit_min) {
        return NK_SUCCESS;  // incomplete_quantifier
      }
      *out_max = UINT32_MAX;
    }
  } else {  // `{n}`
    if (!has_explicit_min) {
      return NK_SUCCESS;  // incomplete quantifier
    }
    *out_allows_reluctant = false;
    *out_max = *out_min;
  }

  if (*out_min > *out_max) {
    set_error_span_to_current(parser, quantifier_begin);
    return NK_ERR_NUMBERS_OUT_OF_ORDER_IN_QUANTIFIER;
  }

  if (code != '}') {
    return NK_SUCCESS;  // incomplete quantifier
  }

  parser->pattern_bytes += width;  // consume `}`

  *out_is_incomplete = false;
  return NK_SUCCESS;
}

static nk_error_t lex_quantifier_type(nk_parser_t* parser, nk_quantifier_type_t* out_type, bool allows_reluctant) {
  *out_type = NK_QUANTIFIER_TYPE_GREEDY;

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    return NK_SUCCESS;
  }

  int8_t width;
  uint32_t code;
  nk_error_t err = peek(parser, &width, &code);
  if (err != NK_SUCCESS) {
    return err;
  }
  if (allows_reluctant && code == '?') {
    *out_type = NK_QUANTIFIER_TYPE_RELUCTANT;
    parser->pattern_bytes += width;
  } else if (code == '+') {
    *out_type = NK_QUANTIFIER_TYPE_POSSESSIVE;
    parser->pattern_bytes += width;
  }

  return NK_SUCCESS;
}

static nk_error_t lex_hexdecimal_number(
  nk_parser_t* parser,
  uint32_t* out_code,
  int min_digits,
  int max_digits,
  nk_error_t incomplete_error
) {
  *out_code = 0;
  int digits = 0;

  while (parser->pattern_bytes < parser->pattern_bytes_end && digits < max_digits) {
    int8_t width;
    uint32_t code;
    nk_error_t err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }

    uint32_t digit_value;
    if (is_decimal_digit(code)) {
      digit_value = code - '0';
    } else if (is_hexdecimal_digit(code)) {
      digit_value = (code & 0x0F) + 9;
    } else {
      break;
    }

    *out_code = (*out_code << 4) | digit_value;
    parser->pattern_bytes += width;
    digits++;
  }

  if (digits < min_digits) {
    return incomplete_error;
  }

  return NK_SUCCESS;
}

static inline void skip_whitespace_in_unicode_brace(nk_parser_t* parser) {
  while (parser->pattern_bytes < parser->pattern_bytes_end) {
    int8_t width;
    uint32_t code;
    nk_error_t err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return;
    }

    bool is_space = (code == ' ' || ('\t' <= code && code <= '\r')) && code != '\n';
    if (!is_space) {
      break;
    }

    parser->pattern_bytes += width;  // consume the whitespace character
  }
}

static nk_error_t lex_unicode_escape(
  nk_parser_t* parser,
  const uint8_t* escape_bytes,
  uint32_t* out_code,
  bool* out_is_unclosed_brace,
  const uint8_t** out_code_bytes,
  const uint8_t** out_code_bytes_end
) {
  *out_code = 0;
  *out_is_unclosed_brace = false;
  *out_code_bytes = NULL;
  *out_code_bytes_end = NULL;

  if ((parser->enc->flags & NK_ENC_FLAG_UNICODE) == 0) {
    set_error_span_to_current(parser, escape_bytes);
    return NK_ERR_UNICODE_ESCAPE_IN_NON_UNICODE_ENCODING;
  }

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    set_error_span_to_current(parser, escape_bytes);
    return NK_ERR_UNCLOSED_UNICODE_ESCAPE_BRACE;
  }

  int8_t width;
  uint32_t code;
  nk_error_t err = peek(parser, &width, &code);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (is_hexdecimal_digit(code)) {
    const uint8_t* codepoint_begin = parser->pattern_bytes;
    nk_error_t err = lex_hexdecimal_number(parser, out_code, 4, 4, NK_ERR_INCOMPLETE_UNICODE_ESCAPE);
    if (err != NK_SUCCESS) {
      if (err == NK_ERR_INCOMPLETE_UNICODE_ESCAPE) {
        set_error_span_to_current(parser, escape_bytes);
      }
      return err;
    }

    *out_code_bytes = codepoint_begin;
    *out_code_bytes_end = parser->pattern_bytes;

    return NK_SUCCESS;
  } else if (code == '{') {
    *out_is_unclosed_brace = true;
    parser->pattern_bytes += width;  // consume `{`
    skip_whitespace_in_unicode_brace(parser);

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      return NK_ERR_UNCLOSED_UNICODE_ESCAPE_BRACE;
    }

    int8_t width;
    uint32_t code;
    nk_error_t err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }
    if (code == '}') {
      set_error_span(parser, escape_bytes, parser->pattern_bytes + width);
      return NK_ERR_EMPTY_UNICODE_ESCAPE_BRACE;
    }

    const uint8_t* codepoint_begin = parser->pattern_bytes;
    err = lex_hexdecimal_number(parser, out_code, 1, 6, NK_ERR_INVALID_UNICODE_ESCAPE);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      return NK_ERR_UNCLOSED_UNICODE_ESCAPE_BRACE;
    }

    err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }
    if (is_hexdecimal_digit(code)) {
      consume_contiguous_hex_digits(parser);
      set_error_span_to_current(parser, codepoint_begin);
      return NK_ERR_CODE_POINT_OUT_OF_RANGE;
    }

    *out_code_bytes = codepoint_begin;
    *out_code_bytes_end = parser->pattern_bytes;

    skip_whitespace_in_unicode_brace(parser);
    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      return NK_ERR_UNCLOSED_UNICODE_ESCAPE_BRACE;
    }

    err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (code == '}') {
      parser->pattern_bytes += width;  // consume `}`
      *out_is_unclosed_brace = false;
    }

    return NK_SUCCESS;
  }

  set_error_span_to_current(parser, escape_bytes);
  return NK_ERR_INCOMPLETE_UNICODE_ESCAPE;
}

static nk_error_t lex_unicode_escape_in_brace(
  nk_parser_t* parser,
  uint32_t* out_code,
  bool* out_is_unclosed_brace,
  const uint8_t** out_code_bytes,
  const uint8_t** out_code_bytes_end
) {
  *out_code = 0;
  *out_is_unclosed_brace = true;
  *out_code_bytes = NULL;
  *out_code_bytes_end = NULL;

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    return NK_ERR_UNCLOSED_UNICODE_ESCAPE_BRACE;
  }

  int8_t width;
  uint32_t code;
  nk_error_t err = peek(parser, &width, &code);
  if (err != NK_SUCCESS) {
    return err;
  }
  if (code == '}') {
    set_error_span(parser, parser->pattern_bytes, parser->pattern_bytes + width);
    return NK_ERR_EMPTY_UNICODE_ESCAPE_BRACE;
  }

  const uint8_t* codepoint_begin = parser->pattern_bytes;
  err = lex_hexdecimal_number(parser, out_code, 1, 6, NK_ERR_INVALID_UNICODE_ESCAPE);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    return NK_ERR_UNCLOSED_UNICODE_ESCAPE_BRACE;
  }

  err = peek(parser, &width, &code);
  if (err != NK_SUCCESS) {
    return err;
  }
  if (is_hexdecimal_digit(code)) {
    consume_contiguous_hex_digits(parser);
    set_error_span_to_current(parser, codepoint_begin);
    return NK_ERR_CODE_POINT_OUT_OF_RANGE;
  }
  *out_code_bytes = codepoint_begin;
  *out_code_bytes_end = parser->pattern_bytes;

  skip_whitespace_in_unicode_brace(parser);

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    return NK_ERR_UNCLOSED_UNICODE_ESCAPE_BRACE;
  }

  err = peek(parser, &width, &code);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (code == '}') {
    parser->pattern_bytes += width;  // consume `}`
    *out_is_unclosed_brace = false;
  }

  return NK_SUCCESS;
}

static nk_error_t
lex_octal_number(nk_parser_t* parser, uint32_t* out_code, int max_digits, nk_error_t incomplete_error) {
  *out_code = 0;
  int digits = 0;

  while (parser->pattern_bytes < parser->pattern_bytes_end && digits < max_digits) {
    int8_t width;
    uint32_t code;
    nk_error_t err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (!is_octal_digit(code)) {
      break;
    }

    *out_code = (*out_code << 3) | (code - '0');
    parser->pattern_bytes += width;
    digits++;
  }

  if (digits == 0) {
    return incomplete_error;
  }

  return NK_SUCCESS;
}

static nk_error_t lex_escape_single_byte(nk_parser_t* parser, const uint8_t* escape_bytes, uint8_t* out_byte) {
  bool retry = true;
  bool control_prefix = false;
  bool meta_prefix = false;

  while (retry) {
    retry = false;

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      set_error_span_to_current(parser, escape_bytes);
      return NK_ERR_INCOMPLETE_ESCAPE;
    }

    int8_t width;
    uint32_t code;
    nk_error_t err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }

    switch (code) {
      case '\\':
        parser->pattern_bytes += width;  // consume `\`
        *out_byte = '\\';
        break;
      case 'n':
        parser->pattern_bytes += width;  // consume `n`
        *out_byte = '\n';
        break;
      case 't':
        parser->pattern_bytes += width;  // consume `t`
        *out_byte = '\t';
        break;
      case 'r':
        parser->pattern_bytes += width;  // consume `r`
        *out_byte = '\r';
        break;
      case 'f':
        parser->pattern_bytes += width;  // consume `f`
        *out_byte = '\f';
        break;
      case 'v':
        parser->pattern_bytes += width;  // consume `v`
        *out_byte = '\v';
        break;
      case 'a':
        parser->pattern_bytes += width;  // consume `a`
        *out_byte = '\a';
        break;
      case 'e':
        parser->pattern_bytes += width;  // consume `e`
        *out_byte = '\x1B';
        break;
      case 'b':
        parser->pattern_bytes += width;  // consume `b`
        *out_byte = '\b';
        break;
      case 's':
        parser->pattern_bytes += width;  // consume `s`
        *out_byte = ' ';
        break;

      case 'x':
      {
        parser->pattern_bytes += width;  // consume `x`

        uint32_t hex_code;
        nk_error_t err = lex_hexdecimal_number(parser, &hex_code, 1, 2, NK_ERR_INCOMPLETE_HEX_ESCAPE);
        if (err != NK_SUCCESS) {
          if (err == NK_ERR_INCOMPLETE_HEX_ESCAPE) {
            set_error_span_to_current(parser, escape_bytes);
          }
          return err;
        }
        *out_byte = (uint8_t)hex_code;
        break;
      }

      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      {
        uint32_t octal_code;
        nk_error_t err = lex_octal_number(parser, &octal_code, 3, NK_ERR_PARSER_BUG);
        if (err != NK_SUCCESS) {
          return err;
        }
        *out_byte = (uint8_t)octal_code;
        break;
      }

      case 'M':
      {
        parser->pattern_bytes += width;  // consume `M`
        if (meta_prefix) {
          set_error_span_to_current(parser, escape_bytes);
          return NK_ERR_DUPLICATE_META_ESCAPE;
        }
        meta_prefix = true;
        if (parser->pattern_bytes >= parser->pattern_bytes_end) {
          set_error_span_to_current(parser, escape_bytes);
          return NK_ERR_INCOMPLETE_META_ESCAPE;
        }

        int8_t width;
        uint32_t code;
        nk_error_t err = consume(parser, &width, &code);
        if (err != NK_SUCCESS) {
          return err;
        }

        if (code != '-' || parser->pattern_bytes >= parser->pattern_bytes_end) {
          set_error_span_to_current(parser, escape_bytes);
          return NK_ERR_INCOMPLETE_META_ESCAPE;
        }

        err = consume(parser, &width, &code);
        if (err != NK_SUCCESS) {
          return err;
        }

        if (code == '\\') {
          retry = true;
          continue;
        }
        if (!is_ascii_printable(code)) {
          set_error_span_to_current(parser, escape_bytes);
          return NK_ERR_INVALID_META_ESCAPE_CODE;
        }

        *out_byte = (uint8_t)code;
        break;
      }

      case 'c':
      case 'C':
      {
        bool needs_hyphen = code == 'C';
        parser->pattern_bytes += width;  // consume `c` or `C`
        if (control_prefix) {
          set_error_span_to_current(parser, escape_bytes);
          return NK_ERR_DUPLICATE_CONTROL_ESCAPE;
        }
        control_prefix = true;

        if (parser->pattern_bytes >= parser->pattern_bytes_end) {
          set_error_span_to_current(parser, escape_bytes);
          return NK_ERR_INCOMPLETE_CONTROL_ESCAPE;
        }

        int8_t width;
        uint32_t code;
        nk_error_t err = consume(parser, &width, &code);
        if (err != NK_SUCCESS) {
          return err;
        }

        if (needs_hyphen) {
          if (code != '-' || parser->pattern_bytes >= parser->pattern_bytes_end) {
            set_error_span_to_current(parser, escape_bytes);
            return NK_ERR_INCOMPLETE_CONTROL_ESCAPE;
          }

          err = consume(parser, &width, &code);
          if (err != NK_SUCCESS) {
            return err;
          }
        }

        if (code == '\\') {
          retry = true;
          continue;
        }
        if (!is_ascii_printable(code)) {
          set_error_span_to_current(parser, escape_bytes);
          return NK_ERR_INVALID_CONTROL_ESCAPE_CODE;
        }
        if (code == '?') {
          control_prefix = false;
          code = 0x7F;
        }
        *out_byte = (uint8_t)code;
        break;
      }

      case '\r':
      {
        if (parser->pattern_bytes < parser->pattern_bytes_end) {
          int8_t next_width;
          uint32_t next_code;
          nk_error_t err = peek(parser, &next_width, &next_code);
          if (err != NK_SUCCESS) {
            return err;
          }

          if (next_code == '\n') {
            code = '\n';
          }
        }
        FALLTHROUGH;
      }

      default:
        if (!is_ascii_printable(code)) {
          set_error_span(parser, escape_bytes, parser->pattern_bytes + width);
          return NK_ERR_INVALID_ESCAPE;
        }

        parser->pattern_bytes += width;
        *out_byte = (uint8_t)code;
        break;
    }
  }

  if (control_prefix) {
    *out_byte &= 0x1F;
  }
  if (meta_prefix) {
    *out_byte |= 0x80;
  }

  return NK_SUCCESS;
}

static nk_error_t lex_escape_bytes(nk_parser_t* parser, const uint8_t* escape_bytes, uint32_t* out_code) {
  uint8_t first_byte;
  nk_error_t err = lex_escape_single_byte(parser, escape_bytes, &first_byte);
  if (err != NK_SUCCESS) {
    return err;
  }

  uint8_t bytes[NK_ENC_MAX_MBC_WIDTH];
  bytes[0] = first_byte;

  int8_t width = nk_enc_scan_mbc_width(parser->enc, bytes, bytes + 1);
  if (width == 0) {
    set_error_span_to_current(parser, escape_bytes);
    return NK_ERR_INVALID_ESCAPED_BYTE_SEQUENCE;
  }

  if (width == 1) {
    *out_code = nk_enc_decode_mbc(parser->enc, bytes, bytes + 1);
    return NK_SUCCESS;
  }

  int8_t remaining_width = -width;
  for (int i = 1; i <= remaining_width; i++) {
    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      set_error_span_to_current(parser, escape_bytes);
      return NK_ERR_INCOMPLETE_ESCAPED_BYTE_SEQUENCE;
    }

    int8_t backslash_width;
    uint32_t backslash_code;
    nk_error_t err = peek(parser, &backslash_width, &backslash_code);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (backslash_code != '\\') {
      set_error_span_to_current(parser, escape_bytes);
      return NK_ERR_INCOMPLETE_ESCAPED_BYTE_SEQUENCE;
    }
    const uint8_t* continued_escape_bytes = parser->pattern_bytes;
    parser->pattern_bytes += backslash_width;  // consume `\`

    err = lex_escape_single_byte(parser, continued_escape_bytes, &bytes[i]);
    if (err != NK_SUCCESS) {
      return err;
    }
  }

  width = nk_enc_scan_mbc_width(parser->enc, bytes, bytes + 1 + remaining_width);
  if (width != 1 + remaining_width) {
    set_error_span_to_current(parser, escape_bytes);
    return NK_ERR_INVALID_ESCAPED_BYTE_SEQUENCE;
  }

  *out_code = nk_enc_decode_mbc(parser->enc, bytes, bytes + 1 + remaining_width);
  return NK_SUCCESS;
}

static nk_error_t lex_name(
  nk_parser_t* parser,
  uint32_t terminator,
  bool with_depth,
  nk_pbuf_t* out_name_buf,
  nk_error_t unterminated_error
) {
  out_name_buf->type = NK_PBUF_VIEW;
  out_name_buf->bytes = out_name_buf->bytes_end = parser->pattern_bytes;

  while (parser->pattern_bytes < parser->pattern_bytes_end) {
    int8_t width;
    uint32_t code;
    nk_error_t err = consume(parser, &width, &code);
    if (err != NK_SUCCESS) {
      nk_pbuf_free(out_name_buf);
      return err;
    }

    if (code == terminator || (with_depth && (code == '+' || code == '-'))) {
      parser->pattern_bytes -= width;  // put back the terminator

      nk_error_t err = pbuf_resize(out_name_buf);
      if (err != NK_SUCCESS) {
        nk_pbuf_free(out_name_buf);
        return err;
      }

      return NK_SUCCESS;
    }

    if (code == '\\') {
      const uint8_t* escape_bytes = parser->pattern_bytes - width;
      int8_t width;
      uint32_t code;
      nk_error_t err = peek(parser, &width, &code);
      if (err != NK_SUCCESS) {
        nk_pbuf_free(out_name_buf);
        return err;
      }

      if (code == 'u') {
        parser->pattern_bytes += width;  // consume `u`

        uint32_t code;
        bool u_is_unclosed_brace;
        const uint8_t* code_bytes;
        const uint8_t* code_bytes_end;
        nk_error_t err =
          lex_unicode_escape(parser, escape_bytes, &code, &u_is_unclosed_brace, &code_bytes, &code_bytes_end);
        if (err != NK_SUCCESS) {
          nk_pbuf_free(out_name_buf);
          return err;
        }

        while (true) {
          nk_error_t err = pbuf_append_code(out_name_buf, parser->enc, code);
          if (err != NK_SUCCESS) {
            nk_pbuf_free(out_name_buf);
            return err;
          }

          if (!u_is_unclosed_brace) {
            break;
          }

          const uint8_t* code_bytes;
          const uint8_t* code_bytes_end;
          err = lex_unicode_escape_in_brace(parser, &code, &u_is_unclosed_brace, &code_bytes, &code_bytes_end);
          if (err != NK_SUCCESS) {
            nk_pbuf_free(out_name_buf);
            return err;
          }
        }

        continue;
      }

      err = lex_escape_bytes(parser, escape_bytes, &code);
      if (err != NK_SUCCESS) {
        nk_pbuf_free(out_name_buf);
        return err;
      }

      err = pbuf_append_code(out_name_buf, parser->enc, code);
      if (err != NK_SUCCESS) {
        nk_pbuf_free(out_name_buf);
        return err;
      }

      continue;
    }

    nk_pbuf_t literal_pbuf = {
      .type = NK_PBUF_VIEW,
      .bytes = parser->pattern_bytes - width,
      .bytes_end = parser->pattern_bytes
    };
    err = pbuf_append(out_name_buf, &literal_pbuf);
    if (err != NK_SUCCESS) {
      nk_pbuf_free(out_name_buf);
      return err;
    }
  }

  nk_pbuf_free(out_name_buf);
  return unterminated_error;
}

static nk_error_t lex_group_num_or_name(
  nk_parser_t* parser,
  uint32_t terminator,
  bool with_depth,
  uint32_t* out_num,
  bool* out_has_name,
  nk_pbuf_t* out_name_buf,
  nk_error_t unterminated_error
) {
  *out_has_name = true;

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    return unterminated_error;
  }

  int8_t width;
  uint32_t code;
  nk_error_t err = peek(parser, &width, &code);
  if (err != NK_SUCCESS) {
    return err;
  }

  int32_t sign = 1;
  const uint8_t* num_begin = parser->pattern_bytes;
  if (is_decimal_digit(code)) {
    *out_has_name = false;
  } else if (code == '-') {
    sign = -1;
    num_begin = parser->pattern_bytes;
    parser->pattern_bytes += width;  // consume `-`

    err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (!is_decimal_digit(code)) {
      set_error_span(parser, num_begin, parser->pattern_bytes + width);
      return NK_ERR_INVALID_GROUP_NAME;
    }

    *out_has_name = false;
  }

  if (*out_has_name) {
    nk_error_t err = lex_name(parser, terminator, with_depth, out_name_buf, unterminated_error);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (out_name_buf->bytes >= out_name_buf->bytes_end) {
      nk_pbuf_free(out_name_buf);
      return NK_ERR_EMPTY_GROUP_NAME;
    }

    return NK_SUCCESS;
  }

  uint32_t num;
  err = lex_decimal_number(parser, &num, parser->back_ref_max_num, NK_ERR_TOO_LARGE_GROUP_NUMBER);
  if (err != NK_SUCCESS) {
    return err;
  }

  int32_t num_or_relative_num = sign * (int32_t)num;
  if (num_or_relative_num >= 0) {
    *out_num = (uint32_t)num_or_relative_num;
  } else {
    if ((int64_t)parser->num_capture_groups + 1 + num_or_relative_num <= 0) {
      set_error_span_to_current(parser, num_begin);
      return NK_ERR_GROUP_NUMBER_OUT_OF_RANGE;
    }

    *out_num = (uint32_t)((int32_t)parser->num_capture_groups + 1 + num_or_relative_num);
  }

  return NK_SUCCESS;
}

static nk_error_t lex_group_num_or_name_with_depth(
  nk_parser_t* parser,
  uint32_t terminator,
  uint32_t* out_num,
  bool* out_has_name,
  nk_pbuf_t* out_name_buf,
  bool* out_has_depth,
  int32_t* out_depth,
  nk_error_t unterminated_error
) {
  *out_has_depth = false;

  nk_error_t err =
    lex_group_num_or_name(parser, terminator, true, out_num, out_has_name, out_name_buf, unterminated_error);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    return NK_SUCCESS;
  }

  int8_t width;
  uint32_t code;
  err = peek(parser, &width, &code);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (code == '+' || code == '-') {
    const uint8_t* depth_begin = parser->pattern_bytes;
    parser->pattern_bytes += width;  // consume `+` or `-`
    int sign = code == '+' ? 1 : -1;

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      if (*out_has_name) {
        nk_pbuf_free(out_name_buf);
      }
      set_error_span_to_current(parser, depth_begin);
      return NK_ERR_INCOMPLETE_CAPTURE_DEPTH;
    }

    err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      if (*out_has_name) {
        nk_pbuf_free(out_name_buf);
      }
      return err;
    }

    uint32_t depth;
    err = lex_decimal_number(parser, &depth, parser->max_capture_depth, NK_ERR_TOO_LARGE_CAPTURE_DEPTH);
    if (err != NK_SUCCESS) {
      if (*out_has_name) {
        nk_pbuf_free(out_name_buf);
      }
      return err;
    }

    *out_has_depth = true;
    *out_depth = sign * (int32_t)depth;
  }

  return NK_SUCCESS;
}

static nk_error_t lex_pending_unicode_escape_in_brace(nk_parser_t* parser, token_t* out_token, bool* out_is_handled) {
  *out_is_handled = false;

  if (!parser->in_unicode_escape_brace) {
    return NK_SUCCESS;
  }

  uint32_t code;
  bool is_unclosed_brace;
  const uint8_t* code_bytes;
  const uint8_t* code_bytes_end;
  nk_error_t err = lex_unicode_escape_in_brace(parser, &code, &is_unclosed_brace, &code_bytes, &code_bytes_end);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (!is_unclosed_brace) {
    parser->in_unicode_escape_brace = false;
  }

  out_token->type = TK_CODE;
  out_token->data.code.value = code;
  out_token->data.code.code_bytes = code_bytes;
  out_token->data.code.code_bytes_end = code_bytes_end;
  *out_is_handled = true;
  return NK_SUCCESS;
}

static inline bool lex_escaped_char_type(uint32_t code, token_t* out_token) {
  out_token->type = TK_CHAR_TYPE;
  switch (code) {
    case 'd':
      out_token->data.char_type.type = NK_CHAR_TYPE_DIGIT;
      out_token->data.char_type.is_positive = true;
      return true;
    case 'D':
      out_token->data.char_type.type = NK_CHAR_TYPE_DIGIT;
      out_token->data.char_type.is_positive = false;
      return true;
    case 'w':
      out_token->data.char_type.type = NK_CHAR_TYPE_WORD;
      out_token->data.char_type.is_positive = true;
      return true;
    case 'W':
      out_token->data.char_type.type = NK_CHAR_TYPE_WORD;
      out_token->data.char_type.is_positive = false;
      return true;
    case 's':
      out_token->data.char_type.type = NK_CHAR_TYPE_SPACE;
      out_token->data.char_type.is_positive = true;
      return true;
    case 'S':
      out_token->data.char_type.type = NK_CHAR_TYPE_SPACE;
      out_token->data.char_type.is_positive = false;
      return true;
    case 'h':
      out_token->data.char_type.type = NK_CHAR_TYPE_HEX_DIGIT;
      out_token->data.char_type.is_positive = true;
      return true;
    case 'H':
      out_token->data.char_type.type = NK_CHAR_TYPE_HEX_DIGIT;
      out_token->data.char_type.is_positive = false;
      return true;
    default:
      return false;
  }
}

static nk_error_t lex_char_prop_escape(nk_parser_t* parser, uint32_t code, token_t* out_token) {
  bool is_positive = code == 'p';

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    report_warning(parser, NK_WARN_INCOMPLETE_CHAR_PROP_ESCAPE, out_token->span_bytes, parser->pattern_bytes);
    out_token->type = TK_CODE;
    out_token->data.code.value = code;  // 'p' or 'P'
    out_token->data.code.code_bytes = out_token->span_bytes;
    out_token->data.code.code_bytes_end = parser->pattern_bytes;
    return NK_SUCCESS;
  }

  int8_t brace_width;
  uint32_t brace_code;
  nk_error_t err = peek(parser, &brace_width, &brace_code);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (brace_code != '{') {
    report_warning(parser, NK_WARN_INCOMPLETE_CHAR_PROP_ESCAPE, out_token->span_bytes, parser->pattern_bytes);
    out_token->type = TK_CODE;
    out_token->data.code.value = code;  // 'p' or 'P'
    out_token->data.code.code_bytes = out_token->span_bytes;
    out_token->data.code.code_bytes_end = parser->pattern_bytes;
    return NK_SUCCESS;
  }

  parser->pattern_bytes += brace_width;  // consume `{`
  const uint8_t* name_bytes_for_error_report = parser->pattern_bytes;

  if (parser->pattern_bytes < parser->pattern_bytes_end) {
    int8_t negation_width;
    uint32_t negation_code;
    nk_error_t err = peek(parser, &negation_width, &negation_code);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (negation_code == '^') {
      is_positive = !is_positive;
      parser->pattern_bytes += negation_width;  // consume `^`
      name_bytes_for_error_report = parser->pattern_bytes;
    }
  }

  nk_pbuf_t name_buf;
  err = lex_name(parser, '}', false, &name_buf, NK_ERR_UNCLOSED_CHAR_PROP_ESCAPE_BRACE);
  if (err != NK_SUCCESS) {
    if (err == NK_ERR_UNCLOSED_CHAR_PROP_ESCAPE_BRACE) {
      set_error_span_to_current(parser, out_token->span_bytes);
    }
    return err;
  }

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    nk_pbuf_free(&name_buf);
    set_error_span_to_current(parser, out_token->span_bytes);
    return NK_ERR_UNCLOSED_CHAR_PROP_ESCAPE_BRACE;
  }

  if (name_buf.bytes >= name_buf.bytes_end) {
    nk_pbuf_free(&name_buf);
    set_error_span_to_current(parser, out_token->span_bytes);
    return NK_ERR_EMPTY_CHAR_PROP_NAME;
  }

  err = consume(parser, &brace_width, &brace_code);
  if (err != NK_SUCCESS) {
    nk_pbuf_free(&name_buf);
    return err;
  }

  if (brace_code != '}') {
    nk_pbuf_free(&name_buf);
    set_error_span_to_current(parser, out_token->span_bytes);
    return NK_ERR_UNCLOSED_CHAR_PROP_ESCAPE_BRACE;
  }

  out_token->type = TK_CHAR_PROP;
  out_token->data.char_prop.is_positive = is_positive;
  err = nk_name_to_cprop(parser->enc, name_buf.bytes, name_buf.bytes_end, &out_token->data.char_prop.cprop);
  if (err != NK_SUCCESS) {
    nk_pbuf_free(&name_buf);
    parser->error_bytes = name_bytes_for_error_report;
    parser->error_bytes_end = parser->pattern_bytes - brace_width;  // point to `}`
    return err;
  }

  nk_pbuf_free(&name_buf);
  return NK_SUCCESS;
}

static nk_error_t lex_common_escape(
  nk_parser_t* parser,
  const uint8_t* escape_bytes,
  int8_t width,
  uint32_t code,
  bool* out_retry,
  bool* out_is_handled,
  token_t* out_token
) {
  *out_retry = false;
  *out_is_handled = false;

  switch (code) {
    case 'u':
    {
      uint32_t escaped_code;
      bool is_unclosed_brace;
      const uint8_t* code_bytes;
      const uint8_t* code_bytes_end;
      nk_error_t err =
        lex_unicode_escape(parser, escape_bytes, &escaped_code, &is_unclosed_brace, &code_bytes, &code_bytes_end);
      if (err != NK_SUCCESS) {
        return err;
      }

      parser->in_unicode_escape_brace = is_unclosed_brace;
      out_token->type = TK_CODE;
      out_token->data.code.value = escaped_code;
      out_token->data.code.code_bytes = code_bytes;
      out_token->data.code.code_bytes_end = code_bytes_end;
      *out_is_handled = true;
      return NK_SUCCESS;
    }

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case 'x':
    case 'c':
    case 'C':
    case 'M':
    case '\\':
    case 'n':
    case 't':
    case 'r':
    case 'f':
    case 'v':
    case 'a':
    case 'e':
    {
      parser->pattern_bytes -= width;

      uint32_t escaped_code;
      nk_error_t err = lex_escape_bytes(parser, escape_bytes, &escaped_code);
      if (err != NK_SUCCESS) {
        return err;
      }

      out_token->type = TK_CODE;
      out_token->data.code.value = escaped_code;
      out_token->data.code.code_bytes = out_token->span_bytes;
      out_token->data.code.code_bytes_end = parser->pattern_bytes;
      *out_is_handled = true;
      return NK_SUCCESS;
    }

    case '\r':
    {
      if (parser->pattern_bytes < parser->pattern_bytes_end) {
        int8_t next_width;
        uint32_t next_code;
        nk_error_t err = peek(parser, &next_width, &next_code);
        if (err != NK_SUCCESS) {
          return err;
        }

        if (next_code == '\n') {
          parser->pattern_bytes += next_width;  // consume `\n` if it's CRLF
          *out_retry = true;
          *out_is_handled = true;
          return NK_SUCCESS;
        }
      }

      out_token->type = TK_CODE;
      out_token->data.code.value = '\r';
      out_token->data.code.code_bytes = out_token->span_bytes;
      out_token->data.code.code_bytes_end = parser->pattern_bytes;
      *out_is_handled = true;
      return NK_SUCCESS;
    }

    case '\n':
      *out_retry = true;
      *out_is_handled = true;
      return NK_SUCCESS;

    default:
      return NK_SUCCESS;
  }
}

static nk_error_t lex_impl(nk_parser_t* parser, token_t* out_token) {
  out_token->span_bytes = parser->pattern_bytes;
  out_token->span_bytes_end = NULL;

  bool has_pending_unicode_escape = false;
  nk_error_t err = lex_pending_unicode_escape_in_brace(parser, out_token, &has_pending_unicode_escape);
  if (err != NK_SUCCESS) {
    return err;
  }
  if (has_pending_unicode_escape) {
    return NK_SUCCESS;
  }

  bool retry = true;

  while (retry) {
    retry = false;
    out_token->span_bytes = parser->pattern_bytes;

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      out_token->type = TK_END;
      return NK_SUCCESS;
    }

    int8_t width;
    uint32_t code;
    nk_error_t err = consume(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }

    switch (code) {
      case ' ':
      case '\t':
      case '\n':
      case '\r':
      case '\f':
        // These whitespace characters are ignored in extended mode.
        if (parser->is_extended_mode) {
          retry = true;
          continue;
        }
        break;

      case '#':
        // In extended mode, `#` is the start of a comment.
        if (parser->is_extended_mode) {
          while (parser->pattern_bytes < parser->pattern_bytes_end) {
            int8_t width;
            uint32_t code;
            nk_error_t err = consume(parser, &width, &code);
            if (err != NK_SUCCESS) {
              return err;
            }

            if (code == '\n') {
              break;
            }
          }
          retry = true;
          continue;
        }
        break;

      case '[':
        out_token->type = TK_CHAR_CLASS_OPEN;
        return NK_SUCCESS;

      case '.':
        out_token->type = TK_DOT;
        return NK_SUCCESS;

      case '^':
        out_token->type = TK_ASSERTION;
        out_token->data.assertion.type = NK_ASSERTION_TYPE_BEGIN_OF_LINE;
        return NK_SUCCESS;

      case '$':
        out_token->type = TK_ASSERTION;
        out_token->data.assertion.type = NK_ASSERTION_TYPE_END_OF_LINE;
        return NK_SUCCESS;

      case '|':
        out_token->type = TK_ALT;
        return NK_SUCCESS;

      case '*':
      {
        out_token->type = TK_QUANTIFIER;
        out_token->data.quantifier.min = 0;
        out_token->data.quantifier.max = UINT32_MAX;
        nk_error_t err = lex_quantifier_type(parser, &out_token->data.quantifier.type, true);
        if (err != NK_SUCCESS) {
          return err;
        }
        return NK_SUCCESS;
      }

      case '+':
      {
        out_token->type = TK_QUANTIFIER;
        out_token->data.quantifier.min = 1;
        out_token->data.quantifier.max = UINT32_MAX;
        nk_error_t err = lex_quantifier_type(parser, &out_token->data.quantifier.type, true);
        if (err != NK_SUCCESS) {
          return err;
        }
        return NK_SUCCESS;
      }

      case '?':
      {
        out_token->type = TK_QUANTIFIER;
        out_token->data.quantifier.min = 0;
        out_token->data.quantifier.max = 1;
        nk_error_t err = lex_quantifier_type(parser, &out_token->data.quantifier.type, true);
        if (err != NK_SUCCESS) {
          return err;
        }
        return NK_SUCCESS;
      }

      case '{':
      {
        const uint8_t* pattern_bytes_backup = parser->pattern_bytes;

        out_token->type = TK_QUANTIFIER;

        bool is_incomplete;
        bool allows_reluctant;
        nk_error_t err = lex_bounded_quantifier(
          parser,
          out_token->span_bytes,
          &out_token->data.quantifier.min,
          &out_token->data.quantifier.max,
          &is_incomplete,
          &allows_reluctant
        );
        if (err != NK_SUCCESS) {
          return err;
        }

        if (is_incomplete) {
          parser->pattern_bytes = pattern_bytes_backup;
          break;
        }

        err = lex_quantifier_type(parser, &out_token->data.quantifier.type, allows_reluctant);
        if (err != NK_SUCCESS) {
          return err;
        }
        return NK_SUCCESS;
      }

      case '(':
      {
        const uint8_t* group_open_begin = parser->pattern_bytes - width;
        if (parser->pattern_bytes < parser->pattern_bytes_end) {
          int8_t width;
          uint32_t code;
          nk_error_t err = peek(parser, &width, &code);
          if (err != NK_SUCCESS) {
            return err;
          }

          if (code == '?') {
            const uint8_t* group_specifier_begin = group_open_begin;
            parser->pattern_bytes += width;  // consume `?`

            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              set_error_span_to_current(parser, group_specifier_begin);
              return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
            }

            err = peek(parser, &width, &code);
            if (err != NK_SUCCESS) {
              return err;
            }

            switch (code) {
              case '#':
                parser->pattern_bytes += width;
                while (parser->pattern_bytes < parser->pattern_bytes_end) {
                  int8_t width;
                  uint32_t code;
                  nk_error_t err = consume(parser, &width, &code);
                  if (err != NK_SUCCESS) {
                    return err;
                  }

                  if (code == ')') {
                    break;
                  }
                }
                retry = true;
                continue;

              case '=':
                parser->pattern_bytes += width;
                out_token->type = TK_LOOKAROUND_OPEN;
                out_token->data.assertion.type = NK_ASSERTION_TYPE_POSITIVE_LOOKAHEAD;
                return NK_SUCCESS;
              case '!':
                parser->pattern_bytes += width;
                out_token->type = TK_LOOKAROUND_OPEN;
                out_token->data.assertion.type = NK_ASSERTION_TYPE_NEGATIVE_LOOKAHEAD;
                return NK_SUCCESS;
              case '>':
                parser->pattern_bytes += width;
                out_token->type = TK_ATOMIC_OPEN;
                return NK_SUCCESS;
              case '~':
                parser->pattern_bytes += width;
                out_token->type = TK_ABSENCE_OPEN;
                return NK_SUCCESS;

              case '<':
              {
                parser->pattern_bytes += width;
                if (parser->pattern_bytes >= parser->pattern_bytes_end) {
                  set_error_span_to_current(parser, group_specifier_begin);
                  return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
                }

                int8_t lookbehind_width;
                uint32_t lookbehind_code;
                nk_error_t err = peek(parser, &lookbehind_width, &lookbehind_code);
                if (err != NK_SUCCESS) {
                  if (err == NK_ERR_INCOMPLETE_GROUP_SPECIFIER) {
                    set_error_span_to_current(parser, group_specifier_begin);
                  }
                  return err;
                }

                if (lookbehind_code == '=') {
                  parser->pattern_bytes += lookbehind_width;
                  out_token->type = TK_LOOKAROUND_OPEN;
                  out_token->data.assertion.type = NK_ASSERTION_TYPE_POSITIVE_LOOKBEHIND;
                  return NK_SUCCESS;
                } else if (lookbehind_code == '!') {
                  parser->pattern_bytes += lookbehind_width;
                  out_token->type = TK_LOOKAROUND_OPEN;
                  out_token->data.assertion.type = NK_ASSERTION_TYPE_NEGATIVE_LOOKBEHIND;
                  return NK_SUCCESS;
                }

                parser->pattern_bytes -= width;  // put back `<`
                FALLTHROUGH;
              }

              case '\'':
              {
                parser->pattern_bytes += width;  // consume `'` or `<`
                const uint8_t* group_name_begin = parser->pattern_bytes;

                uint32_t name_terminator = code == '\'' ? '\'' : '>';
                uint32_t group_num;
                bool has_name;
                nk_pbuf_t name_buf;
                nk_error_t err = lex_group_num_or_name(
                  parser,
                  name_terminator,
                  false,
                  &group_num,
                  &has_name,
                  &name_buf,
                  NK_ERR_INCOMPLETE_GROUP_SPECIFIER
                );
                if (err != NK_SUCCESS) {
                  return err;
                }

                if (!has_name) {
                  set_error_span_to_current(parser, group_name_begin);
                  return NK_ERR_INVALID_GROUP_NAME;
                }

                err = peek(parser, &width, &code);
                if (err != NK_SUCCESS) {
                  nk_pbuf_free(&name_buf);
                  return err;
                }

                if (code != name_terminator) {
                  nk_pbuf_free(&name_buf);
                  set_error_span_to_current(parser, group_specifier_begin);
                  return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
                }
                parser->pattern_bytes += width;  // consume `'` or `>`

                out_token->type = TK_NAMED_GROUP_OPEN;
                out_token->data.named_group.name_buf = name_buf;

                return NK_SUCCESS;
              }

              case '(':
              {
                parser->pattern_bytes += width;  // consume `(`

                if (parser->pattern_bytes >= parser->pattern_bytes_end) {
                  set_error_span_to_current(parser, group_specifier_begin);
                  return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
                }

                int8_t width;
                uint32_t code;
                nk_error_t err = peek(parser, &width, &code);
                if (err != NK_SUCCESS) {
                  return err;
                }

                const uint8_t* name_bytes_for_error_report = parser->pattern_bytes;
                const uint8_t* name_bytes_end_for_error_report = parser->pattern_bytes;

                uint32_t group_num;
                bool has_name;
                nk_pbuf_t name_buf;
                bool has_depth;
                int32_t depth;

                if (is_decimal_digit(code) || code == '-') {
                  nk_error_t err = lex_group_num_or_name_with_depth(
                    parser,
                    ')',
                    &group_num,
                    &has_name,
                    &name_buf,
                    &has_depth,
                    &depth,
                    NK_ERR_INCOMPLETE_GROUP_SPECIFIER
                  );
                  if (err != NK_SUCCESS) {
                    if (err == NK_ERR_INCOMPLETE_GROUP_SPECIFIER) {
                      set_error_span_to_current(parser, group_specifier_begin);
                    }
                    return err;
                  }
                  name_bytes_end_for_error_report = parser->pattern_bytes;
                } else if (code == '\'' || code == '<') {
                  parser->pattern_bytes += width;  // consume `'` or `<`
                  name_bytes_for_error_report = parser->pattern_bytes;

                  uint32_t name_terminator = code == '\'' ? '\'' : '>';
                  nk_error_t err = lex_group_num_or_name_with_depth(
                    parser,
                    name_terminator,
                    &group_num,
                    &has_name,
                    &name_buf,
                    &has_depth,
                    &depth,
                    NK_ERR_INCOMPLETE_GROUP_SPECIFIER
                  );
                  if (err != NK_SUCCESS) {
                    if (err == NK_ERR_INCOMPLETE_GROUP_SPECIFIER) {
                      set_error_span_to_current(parser, group_specifier_begin);
                    }
                    return err;
                  }

                  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
                    if (has_name) {
                      nk_pbuf_free(&name_buf);
                    }
                    set_error_span_to_current(parser, group_specifier_begin);
                    return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
                  }
                  name_bytes_end_for_error_report = parser->pattern_bytes;

                  err = peek(parser, &width, &code);
                  if (err != NK_SUCCESS) {
                    if (has_name) {
                      nk_pbuf_free(&name_buf);
                    }
                    return err;
                  }

                  if (code != name_terminator) {
                    if (has_name) {
                      nk_pbuf_free(&name_buf);
                    }
                    set_error_span_to_current(parser, group_specifier_begin);
                    return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
                  }

                  parser->pattern_bytes += width;  // consume `'` or `>`
                } else {
                  set_error_span_to_current(parser, group_specifier_begin);
                  return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
                }

                if (!has_name && group_num == 0) {
                  parser->error_bytes = name_bytes_for_error_report;
                  parser->error_bytes_end = name_bytes_end_for_error_report;
                  return NK_ERR_INVALID_CONDITIONAL_GROUP_NUMBER;
                }

                if (has_name && name_buf.bytes >= name_buf.bytes_end) {
                  nk_pbuf_free(&name_buf);
                  parser->error_bytes = name_bytes_for_error_report;
                  parser->error_bytes_end = name_bytes_end_for_error_report;
                  return NK_ERR_EMPTY_GROUP_NAME;
                }

                if (parser->pattern_bytes >= parser->pattern_bytes_end) {
                  if (has_name) {
                    nk_pbuf_free(&name_buf);
                  }
                  set_error_span_to_current(parser, group_specifier_begin);
                  return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
                }

                err = peek(parser, &width, &code);
                if (err != NK_SUCCESS) {
                  if (has_name) {
                    nk_pbuf_free(&name_buf);
                  }
                  return err;
                }

                if (code != ')') {
                  if (has_name) {
                    nk_pbuf_free(&name_buf);
                  }
                  set_error_span_to_current(parser, group_specifier_begin);
                  return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
                }
                parser->pattern_bytes += width;  // consume `)`

                out_token->type = TK_CONDITIONAL_OPEN;
                out_token->data.back_ref.group_num = has_name ? 0 : group_num;
                out_token->data.back_ref.has_name = has_name;
                if (has_name) {
                  out_token->data.back_ref.name_buf = name_buf;
                }
                out_token->data.back_ref.has_depth = has_depth;
                out_token->data.back_ref.depth = has_depth ? depth : 0;

                return NK_SUCCESS;
              }

              case 'i':
              case 'm':
              case 'x':
              case 'v':
              case 'd':
              case 'a':
              case 'u':
              case 'S':
              case 'F':
              case 'A':
              case 'T':
              case 'I':
              case '-':
              case ':':
              {
                out_token->data.option.is_extended_mode = parser->is_extended_mode;
                out_token->data.option.is_ignore_case = parser->is_ignore_case;
                out_token->data.option.dot_allows_newline = parser->dot_allows_newline;
                out_token->data.option.char_class_is_strict = parser->char_class_is_strict;
                out_token->data.option.char_type_is_ascii_only = parser->char_type_is_ascii_only;
                out_token->data.option.posix_char_class_is_ascii_only = parser->posix_char_class_is_ascii_only;
                out_token->data.option.fold_flags = parser->fold_flags;

                bool is_positive = true;

                while (parser->pattern_bytes < parser->pattern_bytes_end) {
                  int8_t width;
                  uint32_t code;
                  nk_error_t err = peek(parser, &width, &code);
                  if (err != NK_SUCCESS) {
                    return err;
                  }

                  switch (code) {
                    case 'i':
                      parser->pattern_bytes += width;
                      out_token->data.option.is_ignore_case = is_positive;
                      break;
                    case 'm':
                      parser->pattern_bytes += width;
                      out_token->data.option.dot_allows_newline = is_positive;
                      break;
                    case 'x':
                      parser->pattern_bytes += width;
                      out_token->data.option.is_extended_mode = is_positive;
                      break;
                    case 'v':
                      parser->pattern_bytes += width;
                      out_token->data.option.char_class_is_strict = is_positive;
                      break;
                    case 'd':
                      if (!is_positive) {
                        set_error_span(parser, parser->pattern_bytes, parser->pattern_bytes + width);
                        return NK_ERR_NON_BOOLEAN_GROUP_OPTION;
                      }
                      parser->pattern_bytes += width;
                      out_token->data.option.char_type_is_ascii_only = true;
                      out_token->data.option.posix_char_class_is_ascii_only = false;
                      break;
                    case 'a':
                      if (!is_positive) {
                        set_error_span(parser, parser->pattern_bytes, parser->pattern_bytes + width);
                        return NK_ERR_NON_BOOLEAN_GROUP_OPTION;
                      }
                      parser->pattern_bytes += width;
                      out_token->data.option.char_type_is_ascii_only = true;
                      out_token->data.option.posix_char_class_is_ascii_only = true;
                      break;
                    case 'u':
                      if (!is_positive) {
                        set_error_span(parser, parser->pattern_bytes, parser->pattern_bytes + width);
                        return NK_ERR_NON_BOOLEAN_GROUP_OPTION;
                      }
                      parser->pattern_bytes += width;
                      out_token->data.option.char_type_is_ascii_only = false;
                      out_token->data.option.posix_char_class_is_ascii_only = false;
                      break;
                    case 'S':
                      if (!is_positive) {
                        set_error_span(parser, parser->pattern_bytes, parser->pattern_bytes + width);
                        return NK_ERR_NON_BOOLEAN_GROUP_OPTION;
                      }
                      parser->pattern_bytes += width;
                      out_token->data.option.fold_flags &= (nk_fold_flag_t)~NK_FOLD_FULL;
                      break;
                    case 'F':
                      if (!is_positive) {
                        set_error_span(parser, parser->pattern_bytes, parser->pattern_bytes + width);
                        return NK_ERR_NON_BOOLEAN_GROUP_OPTION;
                      }
                      parser->pattern_bytes += width;
                      out_token->data.option.fold_flags |= NK_FOLD_FULL;
                      break;
                    case 'A':
                      parser->pattern_bytes += width;
                      if (is_positive) {
                        out_token->data.option.fold_flags |= NK_FOLD_ASCII_ONLY;
                      } else {
                        out_token->data.option.fold_flags &= (nk_fold_flag_t)~NK_FOLD_ASCII_ONLY;
                      }
                      break;
                    case 'T':
                      parser->pattern_bytes += width;
                      if (is_positive) {
                        out_token->data.option.fold_flags |= NK_FOLD_TURKISH_AZERI;
                      } else {
                        out_token->data.option.fold_flags &= (nk_fold_flag_t)~NK_FOLD_TURKISH_AZERI;
                      }
                      break;
                    case 'I':
                      parser->pattern_bytes += width;
                      if (is_positive) {
                        out_token->data.option.is_ignore_case = true;
                        out_token->data.option.fold_flags &= (nk_fold_flag_t)~NK_FOLD_FULL;
                        out_token->data.option.fold_flags |= NK_FOLD_ASCII_ONLY;
                      } else {
                        out_token->data.option.is_ignore_case = false;
                      }
                      break;
                    case '-':
                      if (!is_positive) {
                        report_warning(
                          parser,
                          NK_WARN_REDUNDANT_GROUP_OPTION_MINUS,
                          parser->pattern_bytes,
                          parser->pattern_bytes + width
                        );
                        parser->pattern_bytes += width;
                        break;
                      }
                      parser->pattern_bytes += width;
                      is_positive = false;
                      break;
                    case ':':
                      parser->pattern_bytes += width;
                      out_token->type = TK_OPTION_GROUP_OPEN;
                      return NK_SUCCESS;
                    case ')':
                      parser->pattern_bytes += width;
                      out_token->type = TK_OPTION;
                      return NK_SUCCESS;
                    default:
                      set_error_span(parser, parser->pattern_bytes, parser->pattern_bytes + width);
                      return NK_ERR_UNDEFINED_GROUP_OPTION;
                  }
                }

                set_error_span_to_current(parser, group_specifier_begin);
                return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
              }

              default:
                set_error_span(parser, parser->pattern_bytes, parser->pattern_bytes + width);
                return NK_ERR_UNDEFINED_GROUP_OPTION;
            }
          }
        }

        out_token->type = TK_GROUP_OPEN;
        return NK_SUCCESS;
      }

      case ')':
      {
        out_token->type = TK_GROUP_CLOSE;
        return NK_SUCCESS;
      }

      case '\\':
      {
        const uint8_t* escape_bytes = parser->pattern_bytes - width;
        if (parser->pattern_bytes >= parser->pattern_bytes_end) {
          return NK_ERR_INCOMPLETE_ESCAPE;
        }

        nk_error_t err = consume(parser, &width, &code);
        if (err != NK_SUCCESS) {
          return err;
        }

        switch (code) {
          // Character types (e.g., `\d`, `\w`, `\s`, `\h`):
          case 'd':
          case 'D':
          case 'w':
          case 'W':
          case 's':
          case 'S':
          case 'h':
          case 'H':
            if (lex_escaped_char_type(code, out_token)) {
              return NK_SUCCESS;
            }
            return NK_ERR_PARSER_BUG;

          // Grahpeme cluster/keep operator/newline:
          case 'X':
            out_token->type = TK_GRAPHEME_CLUSTER;
            return NK_SUCCESS;
          case 'K':
            out_token->type = TK_KEEP;
            return NK_SUCCESS;
          case 'R':
            out_token->type = TK_NEWLINE;
            return NK_SUCCESS;

          // Assertions:
          case 'b':
            out_token->type = TK_ASSERTION;
            out_token->data.assertion.type = NK_ASSERTION_TYPE_WORD_BOUNDARY;
            return NK_SUCCESS;
          case 'B':
            out_token->type = TK_ASSERTION;
            out_token->data.assertion.type = NK_ASSERTION_TYPE_NON_WORD_BOUNDARY;
            return NK_SUCCESS;
          case 'A':
            out_token->type = TK_ASSERTION;
            out_token->data.assertion.type = NK_ASSERTION_TYPE_BEGIN_OF_STRING;
            return NK_SUCCESS;
          case 'z':
            out_token->type = TK_ASSERTION;
            out_token->data.assertion.type = NK_ASSERTION_TYPE_END_OF_STRING_STRICT;
            return NK_SUCCESS;
          case 'Z':
            out_token->type = TK_ASSERTION;
            out_token->data.assertion.type = NK_ASSERTION_TYPE_END_OF_STRING_LOOSE;
            return NK_SUCCESS;
          case 'G':
            out_token->type = TK_ASSERTION;
            out_token->data.assertion.type = NK_ASSERTION_TYPE_BEGIN_OF_MATCHING;
            return NK_SUCCESS;

          // Character (Unicode) properties (`\p{...}` and `\P{...}`):
          case 'p':
          case 'P':
          {
            nk_error_t err = lex_char_prop_escape(parser, code, out_token);
            if (err != NK_SUCCESS) {
              return err;
            }
            return NK_SUCCESS;
          }

          // Named back-reference (`\k<name>`, `\k'name'`):
          case 'k':
          {
            const uint8_t* back_ref_begin = out_token->span_bytes;
            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              report_warning(parser, NK_WARN_INCOMPLETE_NAMED_BACK_REF_ESCAPE, back_ref_begin, parser->pattern_bytes);
              out_token->type = TK_CODE;
              out_token->data.code.value = 'k';
              out_token->data.code.code_bytes = out_token->span_bytes;
              out_token->data.code.code_bytes_end = parser->pattern_bytes;
              return NK_SUCCESS;
            }

            int8_t next_width;
            uint32_t next_code;
            nk_error_t err = peek(parser, &next_width, &next_code);
            if (err != NK_SUCCESS) {
              return err;
            }

            if (next_code != '<' && next_code != '\'') {
              report_warning(parser, NK_WARN_INCOMPLETE_NAMED_BACK_REF_ESCAPE, back_ref_begin, parser->pattern_bytes);
              out_token->type = TK_CODE;
              out_token->data.code.value = 'k';
              out_token->data.code.code_bytes = out_token->span_bytes;
              out_token->data.code.code_bytes_end = parser->pattern_bytes;
              return NK_SUCCESS;
            }

            parser->pattern_bytes += next_width;  // consume `<` or `'`
            const uint8_t* name_bytes_for_error_report = parser->pattern_bytes;

            uint32_t name_terminator = next_code == '\'' ? '\'' : '>';
            bool has_name = true;
            nk_pbuf_t name_buf;
            uint32_t group_num;
            bool has_depth;
            int32_t depth;
            err = lex_group_num_or_name_with_depth(
              parser,
              name_terminator,
              &group_num,
              &has_name,
              &name_buf,
              &has_depth,
              &depth,
              NK_ERR_INCOMPLETE_BACK_REF
            );
            if (err != NK_SUCCESS) {
              if (err == NK_ERR_INCOMPLETE_BACK_REF) {
                set_error_span_to_current(parser, back_ref_begin);
              }
              return err;
            }

            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              if (has_name) {
                nk_pbuf_free(&name_buf);
              }
              set_error_span_to_current(parser, back_ref_begin);
              return NK_ERR_INCOMPLETE_BACK_REF;
            }

            if (has_name && name_buf.bytes >= name_buf.bytes_end) {
              if (has_name) {
                nk_pbuf_free(&name_buf);
              }
              parser->error_bytes = name_bytes_for_error_report;
              parser->error_bytes_end = parser->pattern_bytes;
              return NK_ERR_EMPTY_GROUP_NAME;
            }

            if (!has_name && group_num == 0) {
              parser->error_bytes = name_bytes_for_error_report;
              parser->error_bytes_end = parser->pattern_bytes;
              return NK_ERR_INVALID_BACK_REF;
            }

            err = consume(parser, &next_width, &next_code);
            if (err != NK_SUCCESS) {
              if (has_name) {
                nk_pbuf_free(&name_buf);
              }
              return err;
            }

            if (next_code != name_terminator) {
              if (has_name) {
                nk_pbuf_free(&name_buf);
              }
              set_error_span_to_current(parser, back_ref_begin);
              return NK_ERR_INCOMPLETE_BACK_REF;
            }

            out_token->type = TK_BACK_REF;
            out_token->data.back_ref.has_name = has_name;
            if (has_name) {
              out_token->data.back_ref.name_buf = name_buf;
            }
            out_token->data.back_ref.group_num = has_name ? 0 : group_num;
            out_token->data.back_ref.has_depth = has_depth;
            out_token->data.back_ref.depth = has_depth ? depth : 0;

            return NK_SUCCESS;
          }

          // Sub-expression call (`\g<name>` or `\g'name'`):
          case 'g':
          {
            const uint8_t* subexp_call_begin = out_token->span_bytes;
            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              report_warning(parser, NK_WARN_INCOMPLETE_SUBEXP_CALL_ESCAPE, subexp_call_begin, parser->pattern_bytes);
              out_token->type = TK_CODE;
              out_token->data.code.value = 'g';
              out_token->data.code.code_bytes = out_token->span_bytes;
              out_token->data.code.code_bytes_end = parser->pattern_bytes;
              return NK_SUCCESS;
            }

            int8_t next_width;
            uint32_t next_code;
            nk_error_t err = peek(parser, &next_width, &next_code);
            if (err != NK_SUCCESS) {
              return err;
            }

            if (next_code != '<' && next_code != '\'') {
              report_warning(parser, NK_WARN_INCOMPLETE_SUBEXP_CALL_ESCAPE, subexp_call_begin, parser->pattern_bytes);
              out_token->type = TK_CODE;
              out_token->data.code.value = 'g';
              out_token->data.code.code_bytes = out_token->span_bytes;
              out_token->data.code.code_bytes_end = parser->pattern_bytes;
              return NK_SUCCESS;
            }

            parser->pattern_bytes += next_width;  // consume `<` or `'`
            const uint8_t* name_bytes_for_error_report = parser->pattern_bytes;

            uint32_t name_terminator = next_code == '\'' ? '\'' : '>';
            uint32_t group_num;
            bool has_name;
            nk_pbuf_t name_buf;
            err = lex_group_num_or_name(
              parser,
              name_terminator,
              false,
              &group_num,
              &has_name,
              &name_buf,
              NK_ERR_INCOMPLETE_SUBEXP_CALL
            );
            if (err != NK_SUCCESS) {
              if (err == NK_ERR_INCOMPLETE_SUBEXP_CALL) {
                set_error_span_to_current(parser, subexp_call_begin);
              }
              return err;
            }

            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              if (has_name) {
                nk_pbuf_free(&name_buf);
              }
              set_error_span_to_current(parser, subexp_call_begin);
              return NK_ERR_INCOMPLETE_SUBEXP_CALL;
            }

            if (has_name && name_buf.bytes >= name_buf.bytes_end) {
              nk_pbuf_free(&name_buf);
              parser->error_bytes = name_bytes_for_error_report;
              parser->error_bytes_end = parser->pattern_bytes;
              return NK_ERR_EMPTY_GROUP_NAME;
            }

            err = consume(parser, &next_width, &next_code);
            if (err != NK_SUCCESS) {
              if (has_name) {
                nk_pbuf_free(&name_buf);
              }
              return err;
            }

            if (next_code != name_terminator) {
              if (has_name) {
                nk_pbuf_free(&name_buf);
              }
              set_error_span_to_current(parser, subexp_call_begin);
              return NK_ERR_INCOMPLETE_SUBEXP_CALL;
            }

            out_token->type = TK_CALL;
            out_token->data.call.has_name = has_name;
            out_token->data.call.group_num = has_name ? 0 : group_num;
            if (has_name) {
              out_token->data.call.name_buf = name_buf;
            }

            return NK_SUCCESS;
          }

          // Back reference (e.g., `\1`, `\2`, ..., `\9`) or octal escape:
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9':
          {
            const uint8_t* pattern_bytes_backup = parser->pattern_bytes;
            parser->pattern_bytes -= width;  // put back the digit for lexing the back-reference number

            uint32_t num;
            nk_error_t err = lex_decimal_number(parser, &num, parser->bare_back_ref_max_num, NK_ERR_INTERNAL_ERROR);
            if (err != NK_SUCCESS && err != NK_ERR_INTERNAL_ERROR) {
              return err;
            }

            if (err == NK_SUCCESS && (num <= 9 || num <= parser->num_capture_groups)) {
              out_token->type = TK_BACK_REF;
              out_token->data.back_ref.group_num = num;
              out_token->data.back_ref.has_name = false;
              out_token->data.back_ref.has_depth = false;
              out_token->data.back_ref.depth = 0;
              return NK_SUCCESS;
            }

            parser->pattern_bytes = pattern_bytes_backup;
            FALLTHROUGH;
          }

          // Other case: treat the escaped character as a code itself.
          default:
          {
            bool is_handled = false;
            nk_error_t err = lex_common_escape(parser, escape_bytes, width, code, &retry, &is_handled, out_token);
            if (err != NK_SUCCESS) {
              return err;
            }
            if (is_handled) {
              if (retry) {
                continue;
              }
              return NK_SUCCESS;
            }

            out_token->type = TK_CODE;
            out_token->data.code.value = code;
            out_token->data.code.code_bytes = out_token->span_bytes;
            out_token->data.code.code_bytes_end = parser->pattern_bytes;
            return NK_SUCCESS;
          }
        }
      }
    }

    if (code == ']') {
      report_warning(
        parser,
        NK_WARN_LITERAL_RIGHT_BRACKET_OUTSIDE_CHAR_CLASS,
        out_token->span_bytes,
        parser->pattern_bytes
      );
    }

    out_token->type = TK_LITERAL;
    out_token->data.literal.bytes = parser->pattern_bytes - width;
    out_token->data.literal.bytes_end = parser->pattern_bytes;

    return NK_SUCCESS;
  }

  return NK_ERR_PARSER_BUG;  // unreachable
}

static inline nk_error_t lex(nk_parser_t* parser, token_t* out_token) {
  nk_error_t err = lex_impl(parser, out_token);
  if (err != NK_SUCCESS) {
    // If an error location is not set yet, we set it to the current position for better error
    // reporting.
    if (parser->error_bytes == NULL) {
      parser->error_bytes = parser->pattern_bytes;
      parser->error_bytes_end = parser->pattern_bytes;
    }

    return err;
  }

  out_token->span_bytes_end = parser->pattern_bytes;
  return NK_SUCCESS;
}

static nk_error_t lex_posix_char_class_name(
  nk_parser_t* parser,
  bool* out_is_closed,
  nk_pbuf_t* out_name_buf,
  const uint8_t** out_name_bytes_end_for_error_report
) {
  *out_is_closed = false;
  out_name_buf->type = NK_PBUF_VIEW;
  out_name_buf->bytes = out_name_buf->bytes_end = parser->pattern_bytes;

  while (parser->pattern_bytes < parser->pattern_bytes_end) {
    int8_t width;
    uint32_t code;
    nk_error_t err = consume(parser, &width, &code);
    if (err != NK_SUCCESS) {
      nk_pbuf_free(out_name_buf);
      return err;
    }

    if (code == '[' || code == ']') {
      nk_pbuf_free(out_name_buf);
      return NK_SUCCESS;  // unclosed
    }

    if (code == ':') {
      if (parser->pattern_bytes >= parser->pattern_bytes_end) {
        nk_pbuf_free(out_name_buf);
        return NK_SUCCESS;  // unclosed
      }

      int8_t bracket_width;
      uint32_t bracket_code;
      err = peek(parser, &bracket_width, &bracket_code);
      if (err != NK_SUCCESS) {
        nk_pbuf_free(out_name_buf);
        return err;
      }

      if (bracket_code == ']') {
        *out_name_bytes_end_for_error_report = parser->pattern_bytes - width;  // exclude `:`
        parser->pattern_bytes += bracket_width;                                // consume `]`
        *out_is_closed = true;
        return NK_SUCCESS;
      }
    }

    if (code == '\\') {
      const uint8_t* escape_bytes = parser->pattern_bytes - width;
      if (parser->pattern_bytes >= parser->pattern_bytes_end) {
        nk_pbuf_free(out_name_buf);
        return NK_ERR_INCOMPLETE_ESCAPE;
      }

      int8_t width;
      uint32_t code;
      nk_error_t err = peek(parser, &width, &code);
      if (err != NK_SUCCESS) {
        nk_pbuf_free(out_name_buf);
        return err;
      }

      if (code == 'u') {
        parser->pattern_bytes += width;  // consume `u`

        uint32_t code;
        bool u_is_unclosed_brace;
        const uint8_t* code_bytes;
        const uint8_t* code_bytes_end;
        nk_error_t err =
          lex_unicode_escape(parser, escape_bytes, &code, &u_is_unclosed_brace, &code_bytes, &code_bytes_end);
        if (err != NK_SUCCESS) {
          nk_pbuf_free(out_name_buf);
          return err;
        }

        while (true) {
          nk_error_t err = pbuf_append_code(out_name_buf, parser->enc, code);
          if (err != NK_SUCCESS) {
            nk_pbuf_free(out_name_buf);
            return err;
          }

          if (!u_is_unclosed_brace) {
            break;
          }

          const uint8_t* code_bytes;
          const uint8_t* code_bytes_end;
          err = lex_unicode_escape_in_brace(parser, &code, &u_is_unclosed_brace, &code_bytes, &code_bytes_end);
          if (err != NK_SUCCESS) {
            nk_pbuf_free(out_name_buf);
            return err;
          }
        }

        continue;
      }

      err = lex_escape_bytes(parser, escape_bytes, &code);
      if (err != NK_SUCCESS) {
        nk_pbuf_free(out_name_buf);
        return err;
      }

      err = pbuf_append_code(out_name_buf, parser->enc, code);
      if (err != NK_SUCCESS) {
        nk_pbuf_free(out_name_buf);
        return err;
      }

      continue;
    }

    nk_pbuf_t literal_pbuf = {
      .type = NK_PBUF_VIEW,
      .bytes = parser->pattern_bytes - width,
      .bytes_end = parser->pattern_bytes
    };
    err = pbuf_append(out_name_buf, &literal_pbuf);
    if (err != NK_SUCCESS) {
      nk_pbuf_free(out_name_buf);
      return err;
    }
  }

  nk_pbuf_free(out_name_buf);
  return NK_SUCCESS;  // unclosed
}

typedef struct poxis_char_class_entry {
  const uint8_t* name;
  nk_posix_char_class_t char_class;
} posix_char_class_entry_t;

static const posix_char_class_entry_t posix_char_class_entries[] = {
  {(const uint8_t*)"alnum", NK_POSIX_CHAR_CLASS_ALNUM},
  {(const uint8_t*)"alpha", NK_POSIX_CHAR_CLASS_ALPHA},
  {(const uint8_t*)"blank", NK_POSIX_CHAR_CLASS_BLANK},
  {(const uint8_t*)"cntrl", NK_POSIX_CHAR_CLASS_CNTRL},
  {(const uint8_t*)"digit", NK_POSIX_CHAR_CLASS_DIGIT},
  {(const uint8_t*)"graph", NK_POSIX_CHAR_CLASS_GRAPH},
  {(const uint8_t*)"lower", NK_POSIX_CHAR_CLASS_LOWER},
  {(const uint8_t*)"print", NK_POSIX_CHAR_CLASS_PRINT},
  {(const uint8_t*)"punct", NK_POSIX_CHAR_CLASS_PUNCT},
  {(const uint8_t*)"space", NK_POSIX_CHAR_CLASS_SPACE},
  {(const uint8_t*)"upper", NK_POSIX_CHAR_CLASS_UPPER},
  {(const uint8_t*)"xdigit", NK_POSIX_CHAR_CLASS_XDIGIT},
};

static nk_error_t name_to_posix_char_class(
  const nk_encoding_t* enc,
  const uint8_t* name_bytes,
  const uint8_t* name_bytes_end,
  nk_posix_char_class_t* out_char_class
) {
  if (name_bytes >= name_bytes_end) {
    return NK_ERR_EMPTY_POSIX_CHAR_CLASS_NAME;
  }

  for (size_t i = 0; i < sizeof(posix_char_class_entries) / sizeof(posix_char_class_entry_t); i++) {
    const posix_char_class_entry_t* entry = &posix_char_class_entries[i];
    const uint8_t* entry_name_bytes = entry->name;
    const uint8_t* name_bytes_for_check = name_bytes;
    while (*entry_name_bytes != '\0' && name_bytes_for_check < name_bytes_end) {
      int8_t width = nk_enc_scan_mbc_width(enc, name_bytes_for_check, name_bytes_end);
      if (width < 0) {
        return NK_ERR_INCOMPLETE_BYTE_SEQUENCE;
      }
      if (width == 0) {
        return NK_ERR_INVALID_BYTE_SEQUENCE;
      }

      uint32_t code = nk_enc_decode_mbc(enc, name_bytes_for_check, name_bytes_end);

      if (code != *entry_name_bytes) {
        break;
      }

      entry_name_bytes++;
      name_bytes_for_check += width;
    }

    if (*entry_name_bytes == '\0' && name_bytes_for_check == name_bytes_end) {
      *out_char_class = entry->char_class;
      return NK_SUCCESS;
    }
  }

  return NK_ERR_INVALID_POSIX_CHAR_CLASS_NAME;
}

typedef enum lex_cc_state {
  CC_STATE_BEGIN,
  CC_STATE_BEGIN_AFTER_NEGATION,
  CC_STATE_WAIT_RANGE_BEGIN,
  CC_STATE_HAS_RANGE_BEGIN,
  CC_STATE_WAIT_RANGE_END,
} lex_cc_state_t;

static nk_error_t lex_in_char_class_impl(nk_parser_t* parser, token_t* out_token, lex_cc_state_t state) {
  out_token->span_bytes = parser->pattern_bytes;
  out_token->span_bytes_end = NULL;

  bool has_pending_unicode_escape = false;
  nk_error_t err = lex_pending_unicode_escape_in_brace(parser, out_token, &has_pending_unicode_escape);
  if (err != NK_SUCCESS) {
    return err;
  }
  if (has_pending_unicode_escape) {
    return NK_SUCCESS;
  }

  bool retry = true;

  while (retry) {
    retry = false;
    out_token->span_bytes = parser->pattern_bytes;

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      out_token->type = TK_END;
      return NK_SUCCESS;
    }

    int8_t width;
    uint32_t code;
    nk_error_t err = consume(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }

    switch (code) {
      case '^':
        if (state == CC_STATE_BEGIN) {
          out_token->type = TK_CHAR_CLASS_NEGATION;
          return NK_SUCCESS;
        }
        break;
      case '-':
      {
        bool is_range_hyphen = state == CC_STATE_HAS_RANGE_BEGIN;
        bool is_first = state == CC_STATE_BEGIN || state == CC_STATE_BEGIN_AFTER_NEGATION;
        bool is_last = false;

        if (parser->pattern_bytes >= parser->pattern_bytes_end) {
          is_range_hyphen = false;
          is_last = true;
        }

        int8_t next_width;
        uint32_t next_code;
        if (parser->pattern_bytes < parser->pattern_bytes_end) {
          nk_error_t err = peek(parser, &next_width, &next_code);
          if (err != NK_SUCCESS) {
            return err;
          }

          if (next_code == ']') {
            // `-]` is not a range hyphen
            is_range_hyphen = false;
            is_last = true;
          }
          if (next_code == '&') {
            parser->pattern_bytes += next_width;  // consume `&`

            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              parser->pattern_bytes -= next_width;  // put back `&`
            } else {
              int8_t next_next_width;
              uint32_t next_next_code;
              nk_error_t err = peek(parser, &next_next_width, &next_next_code);
              if (err != NK_SUCCESS) {
                return err;
              }

              if (next_next_code == '&') {  // `&&`
                // `-&&` is not a range hyphen
                is_range_hyphen = false;
              }
              parser->pattern_bytes -= next_width;  // put back `&`
            }
          }
        }

        if (is_range_hyphen) {
          out_token->type = TK_CHAR_CLASS_RANGE_HYPHEN;
          return NK_SUCCESS;
        }

        if (!is_first && !is_last) {
          report_warning(parser, NK_WARN_LITERAL_HYPHEN_IN_CHAR_CLASS, out_token->span_bytes, parser->pattern_bytes);
        }

        out_token->type = TK_CHAR_CLASS_LITERAL_HYPHEN;
        out_token->data.literal_hyphen.is_first = is_first;
        out_token->data.literal_hyphen.is_last = is_last;

        return NK_SUCCESS;
      }
      case '&':
      {
        if (parser->pattern_bytes >= parser->pattern_bytes_end) {
          break;
        }

        int8_t next_width;
        uint32_t next_code;
        nk_error_t err = peek(parser, &next_width, &next_code);
        if (err != NK_SUCCESS) {
          return err;
        }

        if (next_code == '&') {
          parser->pattern_bytes += next_width;  // consume `&`
          out_token->type = TK_CHAR_CLASS_INTERSECTION;
          return NK_SUCCESS;
        }
        break;
      }
      case '[':
      {
        if (parser->pattern_bytes >= parser->pattern_bytes_end) {
          out_token->type = TK_CHAR_CLASS_OPEN;
          return NK_SUCCESS;
        }

        int8_t colon_width;
        uint32_t colon_code;
        nk_error_t err = peek(parser, &colon_width, &colon_code);
        if (err != NK_SUCCESS) {
          return err;
        }

        if (colon_code == ':') {
          const uint8_t* pattern_bytes_backup = parser->pattern_bytes;
          parser->pattern_bytes += colon_width;  // consume `:`

          const uint8_t* name_bytes_for_error_report = parser->pattern_bytes;

          bool is_positive = true;
          if (parser->pattern_bytes < parser->pattern_bytes_end) {
            int8_t negate_width;
            uint32_t negate_code;
            nk_error_t err = peek(parser, &negate_width, &negate_code);
            if (err != NK_SUCCESS) {
              return err;
            }

            if (negate_code == '^') {
              parser->pattern_bytes += negate_width;  // consume `^`
              name_bytes_for_error_report = parser->pattern_bytes;
              is_positive = false;
            }
          }

          bool is_closed = false;
          nk_pbuf_t name_buf;
          const uint8_t* name_bytes_end_for_error_report;
          nk_error_t err = lex_posix_char_class_name(parser, &is_closed, &name_buf, &name_bytes_end_for_error_report);
          if (err != NK_SUCCESS) {
            return err;
          }

          if (is_closed) {
            nk_posix_char_class_t char_class;
            nk_error_t err = name_to_posix_char_class(parser->enc, name_buf.bytes, name_buf.bytes_end, &char_class);
            if (err != NK_SUCCESS) {
              nk_pbuf_free(&name_buf);
              parser->error_bytes = name_bytes_for_error_report;
              parser->error_bytes_end = name_bytes_end_for_error_report;
              return err;
            }

            nk_pbuf_free(&name_buf);

            out_token->type = TK_POSIX_CHAR_CLASS;
            out_token->data.posix_char_class.is_positive = is_positive;
            out_token->data.posix_char_class.char_class = char_class;
            return NK_SUCCESS;
          }

          parser->pattern_bytes = pattern_bytes_backup;
        }

        // Onigmo treats `[` as a literal if a pattern contains `:]` after `[:`
        // and the distance is 21 or more characters. This rule seems strange
        // and unreasonable. Furthermore, a pattern such as `"[" + "[:" * N + "x" * 21 + "]"`
        // needs O(N^2) parsing time, so Naraku abandon this rule.

        out_token->type = TK_CHAR_CLASS_OPEN;
        return NK_SUCCESS;
      }
      case ']':
        if (
          (state != CC_STATE_BEGIN && state != CC_STATE_BEGIN_AFTER_NEGATION) ||
          parser->pattern_bytes >= parser->pattern_bytes_end
        ) {
          out_token->type = TK_CHAR_CLASS_CLOSE;
          return NK_SUCCESS;
        }
        report_warning(
          parser,
          NK_WARN_LITERAL_RIGHT_BRACKET_IN_CHAR_CLASS,
          out_token->span_bytes,
          parser->pattern_bytes
        );
        break;
      case '\\':
      {
        const uint8_t* escape_bytes = parser->pattern_bytes - width;
        if (parser->pattern_bytes >= parser->pattern_bytes_end) {
          return NK_ERR_INCOMPLETE_ESCAPE;
        }

        int8_t width;
        uint32_t code;
        nk_error_t err = consume(parser, &width, &code);
        if (err != NK_SUCCESS) {
          return err;
        }

        switch (code) {
          // Character types (e.g., `\d`, `\w`, `\s`, `\h`):
          case 'd':
          case 'D':
          case 'w':
          case 'W':
          case 's':
          case 'S':
          case 'h':
          case 'H':
            if (lex_escaped_char_type(code, out_token)) {
              return NK_SUCCESS;
            }
            return NK_ERR_PARSER_BUG;

          // Character (Unicode) properties (`\p{...}` and `\P{...}`):
          case 'p':
          case 'P':
          {
            nk_error_t err = lex_char_prop_escape(parser, code, out_token);
            if (err != NK_SUCCESS) {
              return err;
            }
            return NK_SUCCESS;
          }

          // Other case: treat the escaped character as a code itself.
          default:
          {
            bool is_handled = false;
            nk_error_t err = lex_common_escape(parser, escape_bytes, width, code, &retry, &is_handled, out_token);
            if (err != NK_SUCCESS) {
              return err;
            }
            if (is_handled) {
              if (retry) {
                continue;
              }
              return NK_SUCCESS;
            }

            out_token->type = TK_CODE;
            out_token->data.code.value = code;
            out_token->data.code.code_bytes = out_token->span_bytes;
            out_token->data.code.code_bytes_end = parser->pattern_bytes;
            return NK_SUCCESS;
          }
        }
      }
    }

    out_token->type = TK_CHAR_CLASS_LITERAL_CODE;
    out_token->data.code.value = code;
    out_token->data.code.code_bytes = out_token->span_bytes;
    out_token->data.code.code_bytes_end = parser->pattern_bytes;

    return NK_SUCCESS;
  }

  return NK_ERR_PARSER_BUG;  // unreachable
}

static inline nk_error_t lex_in_char_class(nk_parser_t* parser, token_t* out_token, lex_cc_state_t state) {
  nk_error_t err = lex_in_char_class_impl(parser, out_token, state);
  if (err != NK_SUCCESS) {
    // If an error location is not set yet, we set it to the current position for better error
    // reporting.
    if (parser->error_bytes == NULL) {
      parser->error_bytes = parser->pattern_bytes;
      parser->error_bytes_end = parser->pattern_bytes;
    }

    return err;
  }

  out_token->span_bytes_end = parser->pattern_bytes;
  return NK_SUCCESS;
}

// ==========================================================================
//
// Parser implementation:
//
// ==========================================================================

static inline size_t span_offset_from_bytes(nk_parser_t* parser, const uint8_t* span_bytes) {
  return (size_t)(span_bytes - parser->pattern_bytes_begin);
}

static inline size_t span_length_from_bytes(const uint8_t* span_bytes, const uint8_t* span_bytes_end) {
  return (size_t)(span_bytes_end - span_bytes);
}

static inline void char_class_unions_free(nk_char_class_union_t** unions, size_t unions_len) {
  for (size_t i = 0; i < unions_len; i++) {
    char_class_union_free(unions[i]);
    unions[i] = NULL;
  }
  free(unions);
}

static inline nk_error_t char_class_union_items_ensure_capacity(nk_char_class_union_t* u, size_t* items_cap) {
  if (u->items_len < *items_cap) {
    return NK_SUCCESS;
  }

  size_t new_items_cap = (*items_cap) * 2;
  nk_char_class_item_t** new_items =
    (nk_char_class_item_t**)realloc(u->items, sizeof(nk_char_class_item_t*) * new_items_cap);
  if (new_items == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  u->items = new_items;
  *items_cap = new_items_cap;
  return NK_SUCCESS;
}

static inline nk_error_t
char_class_unions_ensure_capacity(nk_char_class_union_t*** unions_ptr, size_t* unions_cap, size_t unions_len) {
  if (unions_len < *unions_cap) {
    return NK_SUCCESS;
  }

  size_t new_unions_cap = (*unions_cap) * 2;
  nk_char_class_union_t** new_unions =
    (nk_char_class_union_t**)realloc(*unions_ptr, sizeof(nk_char_class_union_t*) * new_unions_cap);
  if (new_unions == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  *unions_ptr = new_unions;
  *unions_cap = new_unions_cap;
  return NK_SUCCESS;
}

static inline nk_error_t enter_parse_depth(nk_parser_t* parser) {
  parser->parse_depth++;
  if (parser->parse_depth > parser->max_parse_depth) {
    set_error_span(parser, parser->pattern_bytes, parser->pattern_bytes);
    parser->parse_depth--;
    return NK_ERR_PARSE_DEPTH_LIMIT_EXCEEDED;
  }
  return NK_SUCCESS;
}

static inline void leave_parse_depth(nk_parser_t* parser) {
  parser->parse_depth--;
}

static nk_error_t parse_char_class_item(nk_parser_t* parser, const token_t* tok, nk_char_class_item_t** out_item_ptr);
static nk_error_t parse_char_class_union(nk_parser_t* parser, token_t* tok, nk_char_class_union_t** out_union_ptr);
static nk_error_t parse_char_class_intersection(
  nk_parser_t* parser,
  const uint8_t* char_class_open_span_bytes,
  bool* out_is_positive,
  size_t* out_unions_len,
  nk_char_class_union_t*** out_unions_ptr
);
static nk_error_t
parse_char_class_item_impl(nk_parser_t* parser, const token_t* tok, nk_char_class_item_t** out_item_ptr);
static nk_error_t parse_char_class_union_impl(nk_parser_t* parser, token_t* tok, nk_char_class_union_t** out_union_ptr);
static nk_error_t parse_char_class_intersection_impl(
  nk_parser_t* parser,
  const uint8_t* char_class_open_span_bytes,
  bool* out_is_positive,
  size_t* out_unions_len,
  nk_char_class_union_t*** out_unions_ptr
);

static nk_error_t
parse_char_class_item_impl(nk_parser_t* parser, const token_t* tok, nk_char_class_item_t** out_item_ptr) {
  *out_item_ptr = NULL;

  nk_char_class_item_t* item = (nk_char_class_item_t*)malloc(sizeof(nk_char_class_item_t));
  if (item == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  item->span_offset = span_offset_from_bytes(parser, tok->span_bytes);
  item->span_length = span_length_from_bytes(tok->span_bytes, tok->span_bytes_end);

  switch (tok->type) {
    case TK_CODE:
    {
      size_t width;
      nk_error_t err = nk_enc_encode_mbc_width(parser->enc, tok->data.code.value, &width);
      if (err != NK_SUCCESS) {
        char_class_item_free(item);
        set_error_span(parser, tok->data.code.code_bytes, tok->data.code.code_bytes_end);
        return err;
      }
      FALLTHROUGH;
    }
    case TK_CHAR_CLASS_LITERAL_CODE:
    {
      item->type = NK_CHAR_CLASS_ITEM_TYPE_CODE;
      item->data.code = tok->data.code.value;
      *out_item_ptr = item;
      return NK_SUCCESS;
    }
    case TK_CHAR_CLASS_LITERAL_HYPHEN:
    {
      item->type = NK_CHAR_CLASS_ITEM_TYPE_CODE;
      item->data.code = '-';
      *out_item_ptr = item;
      return NK_SUCCESS;
    }
    case TK_CHAR_TYPE:
    {
      item->type = NK_CHAR_CLASS_ITEM_TYPE_CHAR_TYPE;
      item->data.char_type.char_type = tok->data.char_type.type;
      item->data.char_type.is_positive = tok->data.char_type.is_positive;
      item->data.char_type.is_ascii_only = parser->char_type_is_ascii_only;
      *out_item_ptr = item;
      return NK_SUCCESS;
    }
    case TK_CHAR_PROP:
    {
      item->type = NK_CHAR_CLASS_ITEM_TYPE_CHAR_PROP;
      item->data.char_prop.cprop = tok->data.char_prop.cprop;
      item->data.char_prop.is_positive = tok->data.char_prop.is_positive;
      *out_item_ptr = item;
      return NK_SUCCESS;
    }
    case TK_POSIX_CHAR_CLASS:
    {
      item->type = NK_CHAR_CLASS_ITEM_TYPE_POSIX_CHAR_CLASS;
      item->data.posix_char_class.posix_char_class = tok->data.posix_char_class.char_class;
      item->data.posix_char_class.is_positive = tok->data.posix_char_class.is_positive;
      item->data.posix_char_class.is_ascii_only = parser->posix_char_class_is_ascii_only;
      *out_item_ptr = item;
      return NK_SUCCESS;
    }
    case TK_CHAR_CLASS_OPEN:
    {
      const uint8_t* span_bytes = tok->span_bytes;
      bool is_positive = true;
      size_t unions_len = 0;
      nk_char_class_union_t** unions = NULL;
      nk_error_t err = parse_char_class_intersection(parser, tok->span_bytes, &is_positive, &unions_len, &unions);
      if (err != NK_SUCCESS) {
        free(item);
        return err;
      }

      item->type = NK_CHAR_CLASS_ITEM_TYPE_NESTED_CHAR_CLASS;
      item->data.nested_char_class.is_positive = is_positive;
      item->data.nested_char_class.unions_len = unions_len;
      item->data.nested_char_class.unions = unions;
      item->span_offset = span_offset_from_bytes(parser, span_bytes);
      item->span_length = span_length_from_bytes(span_bytes, parser->pattern_bytes);
      *out_item_ptr = item;

      return NK_SUCCESS;
    }
    default:
      free(item);
      return NK_ERR_PARSER_BUG;
  }
}

static nk_error_t
parse_char_class_union_impl(nk_parser_t* parser, token_t* tok, nk_char_class_union_t** out_union_ptr) {
  *out_union_ptr = NULL;
  const uint8_t* span_bytes = tok->span_bytes;

  nk_char_class_union_t* u = (nk_char_class_union_t*)malloc(sizeof(nk_char_class_union_t));
  if (u == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  u->span_offset = span_offset_from_bytes(parser, span_bytes);
  u->span_length = 0;
  u->items_len = 0;
  u->items = NULL;

  if (tok->type == TK_CHAR_CLASS_CLOSE || tok->type == TK_CHAR_CLASS_INTERSECTION || tok->type == TK_END) {
    u->span_length = 0;
    *out_union_ptr = u;
    return NK_SUCCESS;
  }
  size_t items_cap = 1;
  u->items = (nk_char_class_item_t**)malloc(sizeof(nk_char_class_item_t*) * items_cap);
  if (u->items == NULL) {
    char_class_union_free(u);
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  nk_char_class_item_t* begin_item = NULL;
  token_t begin_tok;

  while (tok->type != TK_CHAR_CLASS_CLOSE && tok->type != TK_CHAR_CLASS_INTERSECTION && tok->type != TK_END) {
    if (tok->type == TK_CHAR_CLASS_RANGE_HYPHEN) {
      if (begin_item == NULL) {
        char_class_union_free(u);
        return NK_ERR_PARSER_BUG;
      }

      if (begin_tok.type == TK_CHAR_CLASS_LITERAL_HYPHEN && begin_tok.data.literal_hyphen.is_first) {
        report_warning(
          parser,
          NK_WARN_LITERAL_HYPHEN_AT_BEGINNING_OF_CHAR_CLASS,
          begin_tok.span_bytes,
          begin_tok.span_bytes_end
        );
      }

      nk_error_t err = lex_in_char_class(parser, tok, CC_STATE_WAIT_RANGE_END);
      if (err != NK_SUCCESS) {
        char_class_union_free(u);
        char_class_item_free(begin_item);
        return err;
      }

      if (tok->type == TK_CHAR_CLASS_LITERAL_HYPHEN && tok->data.literal_hyphen.is_last) {
        report_warning(parser, NK_WARN_LITERAL_HYPHEN_AT_END_OF_CHAR_CLASS, tok->span_bytes, tok->span_bytes_end);
      }

      if (begin_item->type != NK_CHAR_CLASS_ITEM_TYPE_CODE) {
        parser->error_bytes = parser->pattern_bytes_begin + begin_item->span_offset;
        parser->error_bytes_end = parser->error_bytes + begin_item->span_length;
        char_class_union_free(u);
        char_class_item_free(begin_item);
        return NK_ERR_INVALID_CHAR_CLASS_RANGE;
      }

      nk_char_class_item_t* end_item = NULL;
      err = parse_char_class_item(parser, tok, &end_item);
      if (err != NK_SUCCESS) {
        char_class_union_free(u);
        char_class_item_free(begin_item);
        return err;
      }

      if (end_item->type != NK_CHAR_CLASS_ITEM_TYPE_CODE) {
        parser->error_bytes = parser->pattern_bytes_begin + end_item->span_offset;
        parser->error_bytes_end = parser->error_bytes + end_item->span_length;
        char_class_union_free(u);
        char_class_item_free(begin_item);
        char_class_item_free(end_item);
        return NK_ERR_INVALID_CHAR_CLASS_RANGE;
      }

      if (begin_item->data.code > end_item->data.code) {
        parser->error_bytes = parser->pattern_bytes_begin + begin_item->span_offset;
        parser->error_bytes_end = parser->pattern_bytes_begin + end_item->span_offset + end_item->span_length;
        char_class_union_free(u);
        char_class_item_free(begin_item);
        char_class_item_free(end_item);
        return NK_ERR_CHAR_CLASS_RANGE_OUT_OF_ORDER;
      }

      uint32_t begin_code = begin_item->data.code;
      uint32_t end_code = end_item->data.code;
      char_class_item_free(end_item);

      begin_item->type = NK_CHAR_CLASS_ITEM_TYPE_RANGE;
      begin_item->data.range.begin_code = begin_code;
      begin_item->data.range.end_code = end_code;
      begin_item->span_offset = span_offset_from_bytes(parser, begin_tok.span_bytes);
      begin_item->span_length = span_length_from_bytes(begin_tok.span_bytes, tok->span_bytes_end);

      err = char_class_union_items_ensure_capacity(u, &items_cap);
      if (err != NK_SUCCESS) {
        char_class_union_free(u);
        char_class_item_free(begin_item);
        return err;
      }

      u->items[u->items_len++] = begin_item;

      begin_item = NULL;

      err = lex_in_char_class(parser, tok, CC_STATE_WAIT_RANGE_BEGIN);
      if (err != NK_SUCCESS) {
        char_class_union_free(u);
        return err;
      }

      if (tok->type == TK_CHAR_CLASS_CLOSE || tok->type == TK_CHAR_CLASS_INTERSECTION || tok->type == TK_END) {
        break;
      }
    }

    if (begin_item != NULL) {
      nk_error_t err = char_class_union_items_ensure_capacity(u, &items_cap);
      if (err != NK_SUCCESS) {
        char_class_union_free(u);
        char_class_item_free(begin_item);
        return err;
      }

      u->items[u->items_len++] = begin_item;

      begin_item = NULL;
    }

    begin_tok = *tok;
    nk_error_t err = parse_char_class_item(parser, tok, &begin_item);
    if (err != NK_SUCCESS) {
      char_class_union_free(u);
      return err;
    }

    err = lex_in_char_class(parser, tok, CC_STATE_HAS_RANGE_BEGIN);
    if (err != NK_SUCCESS) {
      char_class_union_free(u);
      char_class_item_free(begin_item);
      return err;
    }
  }

  if (begin_item != NULL) {
    nk_error_t err = char_class_union_items_ensure_capacity(u, &items_cap);
    if (err != NK_SUCCESS) {
      char_class_union_free(u);
      char_class_item_free(begin_item);
      return err;
    }

    u->items[u->items_len++] = begin_item;

    begin_item = NULL;
  }

  if (u->items_len < items_cap) {
    nk_char_class_item_t** resized_items =
      (nk_char_class_item_t**)realloc(u->items, sizeof(nk_char_class_item_t*) * u->items_len);
    if (resized_items == NULL) {
      char_class_union_free(u);
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    u->items = resized_items;
  }

  u->span_length = span_length_from_bytes(span_bytes, tok->span_bytes);
  *out_union_ptr = u;

  return NK_SUCCESS;
}

static nk_error_t parse_char_class_intersection_impl(
  nk_parser_t* parser,
  const uint8_t* char_class_open_span_bytes,
  bool* out_is_positive,
  size_t* out_unions_len,
  nk_char_class_union_t*** out_unions_ptr
) {
  *out_is_positive = true;
  *out_unions_len = 0;
  *out_unions_ptr = NULL;

  token_t tok;
  nk_error_t err = lex_in_char_class(parser, &tok, CC_STATE_BEGIN);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (tok.type == TK_CHAR_CLASS_NEGATION) {
    *out_is_positive = false;

    err = lex_in_char_class(parser, &tok, CC_STATE_BEGIN_AFTER_NEGATION);
    if (err != NK_SUCCESS) {
      return err;
    }
  }

  if (tok.type == TK_CHAR_CLASS_CLOSE) {
    parser->error_bytes = char_class_open_span_bytes;
    parser->error_bytes_end = tok.span_bytes_end;
    return NK_ERR_EMPTY_CHAR_CLASS;
  }

  nk_char_class_union_t* u = NULL;
  err = parse_char_class_union(parser, &tok, &u);
  if (err != NK_SUCCESS) {
    return err;
  }

  size_t unions_len = 1;
  size_t unions_cap = 1;
  nk_char_class_union_t** unions = (nk_char_class_union_t**)malloc(sizeof(nk_char_class_union_t*) * unions_cap);
  if (unions == NULL) {
    char_class_union_free(u);
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  unions[0] = u;

  while (tok.type == TK_CHAR_CLASS_INTERSECTION) {
    nk_error_t err = lex_in_char_class(parser, &tok, CC_STATE_WAIT_RANGE_BEGIN);
    if (err != NK_SUCCESS) {
      char_class_unions_free(unions, unions_len);
      return err;
    }

    nk_char_class_union_t* u = NULL;
    err = parse_char_class_union(parser, &tok, &u);
    if (err != NK_SUCCESS) {
      char_class_unions_free(unions, unions_len);
      return err;
    }

    err = char_class_unions_ensure_capacity(&unions, &unions_cap, unions_len);
    if (err != NK_SUCCESS) {
      char_class_unions_free(unions, unions_len);
      char_class_union_free(u);
      return err;
    }

    unions[unions_len++] = u;
  }

  if (unions_len < unions_cap) {
    nk_char_class_union_t** resized_unions =
      (nk_char_class_union_t**)realloc(unions, sizeof(nk_char_class_union_t*) * unions_len);
    if (resized_unions == NULL) {
      char_class_unions_free(unions, unions_len);
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    unions = resized_unions;
  }

  if (tok.type != TK_CHAR_CLASS_CLOSE) {
    char_class_unions_free(unions, unions_len);
    parser->error_bytes = tok.span_bytes;
    parser->error_bytes_end = tok.span_bytes_end;
    return NK_ERR_UNTERMINATED_CHAR_CLASS;
  }

  *out_unions_len = unions_len;
  *out_unions_ptr = unions;

  return NK_SUCCESS;
}

static nk_error_t parse_char_class_item(nk_parser_t* parser, const token_t* tok, nk_char_class_item_t** out_item_ptr) {
  nk_error_t err = enter_parse_depth(parser);
  if (err != NK_SUCCESS) {
    return err;
  }

  err = parse_char_class_item_impl(parser, tok, out_item_ptr);
  leave_parse_depth(parser);
  return err;
}

static nk_error_t parse_char_class_union(nk_parser_t* parser, token_t* tok, nk_char_class_union_t** out_union_ptr) {
  nk_error_t err = enter_parse_depth(parser);
  if (err != NK_SUCCESS) {
    return err;
  }

  err = parse_char_class_union_impl(parser, tok, out_union_ptr);
  leave_parse_depth(parser);
  return err;
}

static nk_error_t parse_char_class_intersection(
  nk_parser_t* parser,
  const uint8_t* char_class_open_span_bytes,
  bool* out_is_positive,
  size_t* out_unions_len,
  nk_char_class_union_t*** out_unions_ptr
) {
  nk_error_t err = enter_parse_depth(parser);
  if (err != NK_SUCCESS) {
    return err;
  }

  err = parse_char_class_intersection_impl(
    parser,
    char_class_open_span_bytes,
    out_is_positive,
    out_unions_len,
    out_unions_ptr
  );
  leave_parse_depth(parser);
  return err;
}

static inline void set_node_span_from_bytes(
  nk_parser_t* parser,
  nk_node_t* node,
  const uint8_t* span_bytes,
  const uint8_t* span_bytes_end
) {
  node->base.span_offset = span_offset_from_bytes(parser, span_bytes);
  node->base.span_length = span_length_from_bytes(span_bytes, span_bytes_end);
}

static inline void set_node_span_from_token(nk_parser_t* parser, nk_node_t* node, const token_t* tok) {
  set_node_span_from_bytes(parser, node, tok->span_bytes, tok->span_bytes_end);
}

static inline const uint8_t* node_span_end_bytes(nk_parser_t* parser, const nk_node_t* node) {
  return parser->pattern_bytes_begin + node->base.span_offset + node->base.span_length;
}

static inline const uint8_t* node_span_begin_bytes(nk_parser_t* parser, const nk_node_t* node) {
  return parser->pattern_bytes_begin + node->base.span_offset;
}

static inline nk_error_t alloc_node(nk_node_type_t type, nk_node_t** out_node_ptr) {
  nk_node_t* node = (nk_node_t*)malloc(sizeof(nk_node_t));
  if (node == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  node->base.type = type;
  *out_node_ptr = node;
  return NK_SUCCESS;
}

static inline nk_error_t
alloc_node_from_token(nk_parser_t* parser, nk_node_type_t type, const token_t* tok, nk_node_t** out_node_ptr) {
  nk_error_t err = alloc_node(type, out_node_ptr);
  if (err != NK_SUCCESS) {
    return err;
  }
  set_node_span_from_token(parser, *out_node_ptr, tok);
  return NK_SUCCESS;
}

static inline nk_error_t alloc_node_from_bytes(
  nk_parser_t* parser,
  nk_node_type_t type,
  const uint8_t* span_bytes,
  const uint8_t* span_bytes_end,
  nk_node_t** out_node_ptr
) {
  nk_error_t err = alloc_node(type, out_node_ptr);
  if (err != NK_SUCCESS) {
    return err;
  }
  set_node_span_from_bytes(parser, *out_node_ptr, span_bytes, span_bytes_end);
  return NK_SUCCESS;
}

static nk_error_t parse_alt(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr);
static nk_error_t parse_concat(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr);
static nk_error_t parse_group_alt_body(nk_parser_t* parser, token_t* tok, nk_node_t** out_child_node_ptr);
static nk_error_t parse_atom(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr);
static nk_error_t parse_quantifier(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr);

static nk_error_t parse_alt_impl(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr);
static nk_error_t parse_concat_impl(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr);
static nk_error_t parse_group_alt_body_impl(nk_parser_t* parser, token_t* tok, nk_node_t** out_child_node_ptr);
static nk_error_t parse_atom_impl(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr);
static nk_error_t parse_quantifier_impl(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr);

static nk_error_t parse_group_alt_body_impl(nk_parser_t* parser, token_t* tok, nk_node_t** out_child_node_ptr) {
  nk_error_t err = lex(parser, tok);
  if (err != NK_SUCCESS) {
    return err;
  }

  nk_node_t* child_node = NULL;
  err = parse_alt(parser, tok, &child_node);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (tok->type != TK_GROUP_CLOSE) {
    nk_node_free(child_node);
    return NK_ERR_UNTERMINATED_GROUP;
  }

  *out_child_node_ptr = child_node;
  return NK_SUCCESS;
}

typedef struct {
  bool is_extended_mode;
  bool is_ignore_case;
  bool dot_allows_newline;
  bool char_class_is_strict;
  bool char_type_is_ascii_only;
  bool posix_char_class_is_ascii_only;
  nk_fold_flag_t fold_flags;
} parser_state_t;

static inline void parser_state_save(nk_parser_t* parser, parser_state_t* out_state) {
  out_state->is_extended_mode = parser->is_extended_mode;
  out_state->is_ignore_case = parser->is_ignore_case;
  out_state->dot_allows_newline = parser->dot_allows_newline;
  out_state->char_class_is_strict = parser->char_class_is_strict;
  out_state->char_type_is_ascii_only = parser->char_type_is_ascii_only;
  out_state->posix_char_class_is_ascii_only = parser->posix_char_class_is_ascii_only;
  out_state->fold_flags = parser->fold_flags;
}

static inline void parser_state_apply_option(nk_parser_t* parser, const token_t* tok) {
  parser->is_extended_mode = tok->data.option.is_extended_mode;
  parser->is_ignore_case = tok->data.option.is_ignore_case;
  parser->dot_allows_newline = tok->data.option.dot_allows_newline;
  parser->char_class_is_strict = tok->data.option.char_class_is_strict;
  parser->char_type_is_ascii_only = tok->data.option.char_type_is_ascii_only;
  parser->posix_char_class_is_ascii_only = tok->data.option.posix_char_class_is_ascii_only;
  parser->fold_flags = tok->data.option.fold_flags;
}

static inline void parser_state_restore(nk_parser_t* parser, const parser_state_t* state) {
  parser->is_extended_mode = state->is_extended_mode;
  parser->is_ignore_case = state->is_ignore_case;
  parser->dot_allows_newline = state->dot_allows_newline;
  parser->char_class_is_strict = state->char_class_is_strict;
  parser->char_type_is_ascii_only = state->char_type_is_ascii_only;
  parser->posix_char_class_is_ascii_only = state->posix_char_class_is_ascii_only;
  parser->fold_flags = state->fold_flags;
}

static nk_error_t parse_atom_impl(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  const uint8_t* atom_span_bytes = tok->span_bytes;

  switch (tok->type) {
    case TK_LITERAL:
    {
      nk_node_t* literal_node = NULL;
      nk_error_t err = alloc_node_from_token(parser, NK_NODE_TYPE_LITERAL, tok, &literal_node);
      if (err != NK_SUCCESS) {
        return err;
      }
      literal_node->literal.buf =
        (nk_pbuf_t){.type = NK_PBUF_VIEW, .bytes = tok->data.literal.bytes, .bytes_end = tok->data.literal.bytes_end};
      literal_node->literal.is_ignore_case = parser->is_ignore_case;
      literal_node->literal.fold_flags = parser->fold_flags;
      *out_node_ptr = literal_node;
      break;
    }
    case TK_CODE:
    {
      nk_node_t* literal_node = NULL;
      nk_error_t err = alloc_node_from_token(parser, NK_NODE_TYPE_LITERAL, tok, &literal_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      uint32_t code = tok->data.code.value;
      uint8_t buf[NK_ENC_MAX_MBC_WIDTH];
      size_t width;
      err = nk_enc_encode_mbc(parser->enc, code, &width, buf);
      if (err != NK_SUCCESS) {
        set_error_span(parser, tok->data.code.code_bytes, tok->data.code.code_bytes_end);
        free(literal_node);
        return err;
      }

      uint8_t* bytes = (uint8_t*)malloc(width);
      if (bytes == NULL) {
        free(literal_node);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }

      memcpy(bytes, buf, width);
      literal_node->literal.buf = (nk_pbuf_t){.type = NK_PBUF_OWNED, .bytes = bytes, .bytes_end = bytes + width};
      literal_node->literal.is_ignore_case = parser->is_ignore_case;
      literal_node->literal.fold_flags = parser->fold_flags;

      *out_node_ptr = literal_node;
      break;
    }
    case TK_CHAR_CLASS_OPEN:
    {
      bool is_positive = true;
      size_t unions_len = 0;
      nk_char_class_union_t** unions = NULL;
      nk_error_t err = parse_char_class_intersection(parser, tok->span_bytes, &is_positive, &unions_len, &unions);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* char_class_node = NULL;
      err = alloc_node_from_bytes(
        parser,
        NK_NODE_TYPE_CHAR_CLASS,
        atom_span_bytes,
        parser->pattern_bytes,
        &char_class_node
      );
      if (err != NK_SUCCESS) {
        char_class_unions_free(unions, unions_len);
        return err;
      }

      char_class_node->char_class.is_strict = parser->char_class_is_strict;
      char_class_node->char_class.is_ignore_case = parser->is_ignore_case;
      char_class_node->char_class.fold_flags = parser->fold_flags;
      char_class_node->char_class.is_positive = is_positive;
      char_class_node->char_class.unions_len = unions_len;
      char_class_node->char_class.unions = unions;

      *out_node_ptr = char_class_node;
      break;
    }
    case TK_DOT:
    {
      nk_node_t* dot_node = NULL;
      nk_error_t err = alloc_node_from_token(parser, NK_NODE_TYPE_DOT, tok, &dot_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      dot_node->dot.allows_newline = parser->dot_allows_newline;

      *out_node_ptr = dot_node;
      break;
    }
    case TK_ASSERTION:
    {
      nk_node_t* assertion_node = NULL;
      nk_error_t err = alloc_node_from_token(parser, NK_NODE_TYPE_ASSERTION, tok, &assertion_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      assertion_node->assertion.type = tok->data.assertion.type;
      assertion_node->assertion.child = NULL;

      *out_node_ptr = assertion_node;
      break;
    }
    case TK_QUANTIFIER:
      return NK_ERR_NOTHING_TO_REPEAT;
    case TK_CHAR_TYPE:
    {
      nk_node_t* char_type_node = NULL;
      nk_error_t err = alloc_node_from_token(parser, NK_NODE_TYPE_CHAR_TYPE, tok, &char_type_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      char_type_node->char_type.char_type = tok->data.char_type.type;
      char_type_node->char_type.is_positive = tok->data.char_type.is_positive;
      char_type_node->char_type.is_ascii_only = parser->char_type_is_ascii_only;
      char_type_node->char_type.is_ignore_case = parser->is_ignore_case;
      char_type_node->char_type.fold_flags = parser->fold_flags;

      *out_node_ptr = char_type_node;
      break;
    }
    case TK_CHAR_PROP:
    {
      nk_node_t* char_prop_node = NULL;
      nk_error_t err = alloc_node_from_token(parser, NK_NODE_TYPE_CHAR_PROP, tok, &char_prop_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      char_prop_node->char_prop.cprop = tok->data.char_prop.cprop;
      char_prop_node->char_prop.is_positive = tok->data.char_prop.is_positive;
      char_prop_node->char_prop.is_ignore_case = parser->is_ignore_case;
      char_prop_node->char_prop.fold_flags = parser->fold_flags;

      *out_node_ptr = char_prop_node;
      break;
    }
    case TK_GRAPHEME_CLUSTER:
    {
      nk_node_t* gc_node = NULL;
      nk_error_t err = alloc_node_from_token(parser, NK_NODE_TYPE_GRAPHEME_CLUSTER, tok, &gc_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      *out_node_ptr = gc_node;
      break;
    }
    case TK_KEEP:
    {
      nk_node_t* keep_node = NULL;
      nk_error_t err = alloc_node_from_token(parser, NK_NODE_TYPE_KEEP, tok, &keep_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      *out_node_ptr = keep_node;
      break;
    }
    case TK_NEWLINE:
    {
      nk_node_t* newline_node = NULL;
      nk_error_t err = alloc_node_from_token(parser, NK_NODE_TYPE_NEWLINE, tok, &newline_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      *out_node_ptr = newline_node;
      break;
    }
    case TK_BACK_REF:
    {
      nk_node_t* back_ref_node = NULL;
      nk_error_t err = alloc_node_from_token(parser, NK_NODE_TYPE_BACK_REF, tok, &back_ref_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      back_ref_node->back_ref.is_ignore_case = parser->is_ignore_case;
      back_ref_node->back_ref.fold_flags = parser->fold_flags;
      back_ref_node->back_ref.has_name = tok->data.back_ref.has_name;
      if (tok->data.back_ref.has_name) {
        back_ref_node->back_ref.name_buf = tok->data.back_ref.name_buf;
      }
      back_ref_node->back_ref.group_num = tok->data.back_ref.group_num;
      back_ref_node->back_ref.has_depth = tok->data.back_ref.has_depth;
      back_ref_node->back_ref.depth = tok->data.back_ref.depth;

      *out_node_ptr = back_ref_node;
      break;
    }
    case TK_CALL:
    {
      nk_node_t* call_node = NULL;
      nk_error_t err = alloc_node_from_token(parser, NK_NODE_TYPE_CALL, tok, &call_node);
      if (err != NK_SUCCESS) {
        return err;
      }
      call_node->call.has_name = tok->data.call.has_name;
      if (tok->data.call.has_name) {
        call_node->call.name_buf = tok->data.call.name_buf;
      }
      call_node->call.group_num = tok->data.call.group_num;

      *out_node_ptr = call_node;
      break;
    }
    case TK_GROUP_OPEN:
    {
      parser->num_capture_groups++;
      if (parser->num_capture_groups > parser->max_group_num) {
        return NK_ERR_TOO_MANY_CAPTURE_GROUPS;
      }

      uint32_t group_num = parser->num_capture_groups;

      nk_node_t* child_node = NULL;
      nk_error_t err = parse_group_alt_body(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* group_node = NULL;
      err = alloc_node_from_bytes(parser, NK_NODE_TYPE_GROUP, atom_span_bytes, tok->span_bytes_end, &group_node);
      if (err != NK_SUCCESS) {
        nk_node_free(child_node);
        return err;
      }

      group_node->group.child = child_node;
      group_node->group.has_name = false;
      group_node->group.group_num = group_num;

      *out_node_ptr = group_node;
      break;
    }
    case TK_NAMED_GROUP_OPEN:
    {
      parser->has_named_groups = true;
      parser->num_capture_groups++;
      if (parser->num_capture_groups > parser->max_group_num) {
        return NK_ERR_TOO_MANY_CAPTURE_GROUPS;
      }

      nk_pbuf_t name_buf = tok->data.named_group.name_buf;

      nk_node_t* child_node = NULL;
      nk_error_t err = parse_group_alt_body(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        nk_pbuf_free(&name_buf);
        return err;
      }

      nk_node_t* group_node = NULL;
      err = alloc_node_from_bytes(parser, NK_NODE_TYPE_GROUP, atom_span_bytes, tok->span_bytes_end, &group_node);
      if (err != NK_SUCCESS) {
        nk_node_free(child_node);
        nk_pbuf_free(&name_buf);
        return err;
      }

      group_node->group.child = child_node;
      group_node->group.has_name = true;
      group_node->group.name_buf = name_buf;
      group_node->group.group_num = 0;

      *out_node_ptr = group_node;
      break;
    }
    case TK_LOOKAROUND_OPEN:
    {
      nk_assertion_type_t type = tok->data.assertion.type;
      nk_node_t* child_node = NULL;
      nk_error_t err = parse_group_alt_body(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* lookaround_node = NULL;
      err =
        alloc_node_from_bytes(parser, NK_NODE_TYPE_ASSERTION, atom_span_bytes, tok->span_bytes_end, &lookaround_node);
      if (err != NK_SUCCESS) {
        nk_node_free(child_node);
        return err;
      }

      lookaround_node->assertion.type = type;
      lookaround_node->assertion.child = child_node;

      *out_node_ptr = lookaround_node;
      break;
    }
    case TK_ATOMIC_OPEN:
    {
      nk_node_t* child_node = NULL;
      nk_error_t err = parse_group_alt_body(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* atomic_node = NULL;
      err = alloc_node_from_bytes(parser, NK_NODE_TYPE_ATOMIC, atom_span_bytes, tok->span_bytes_end, &atomic_node);
      if (err != NK_SUCCESS) {
        nk_node_free(child_node);
        return err;
      }

      atomic_node->atomic.child = child_node;

      *out_node_ptr = atomic_node;
      break;
    }
    case TK_ABSENCE_OPEN:
    {
      nk_node_t* child_node = NULL;
      nk_error_t err = parse_group_alt_body(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* absence_node = NULL;
      err = alloc_node_from_bytes(parser, NK_NODE_TYPE_ABSENCE, atom_span_bytes, tok->span_bytes_end, &absence_node);
      if (err != NK_SUCCESS) {
        nk_node_free(child_node);
        return err;
      }

      absence_node->absence.child = child_node;

      *out_node_ptr = absence_node;
      break;
    }
    case TK_OPTION_GROUP_OPEN:
    {
      parser_state_t saved_state;
      parser_state_save(parser, &saved_state);
      parser_state_apply_option(parser, tok);

      nk_node_t* child_node = NULL;
      nk_error_t err = parse_group_alt_body(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        parser_state_restore(parser, &saved_state);
        return err;
      }

      nk_node_t* group_node = NULL;
      err = alloc_node_from_bytes(parser, NK_NODE_TYPE_GROUP, atom_span_bytes, tok->span_bytes_end, &group_node);
      if (err != NK_SUCCESS) {
        parser_state_restore(parser, &saved_state);
        nk_node_free(child_node);
        return err;
      }

      parser_state_restore(parser, &saved_state);

      group_node->group.child = child_node;
      group_node->group.has_name = false;
      group_node->group.group_num = 0;

      *out_node_ptr = group_node;
      break;
    }
    case TK_OPTION:
    {
      parser_state_t saved_state;
      parser_state_save(parser, &saved_state);
      parser_state_apply_option(parser, tok);

      nk_error_t err = lex(parser, tok);
      if (err != NK_SUCCESS) {
        parser_state_restore(parser, &saved_state);
        return err;
      }

      nk_node_t* child_node = NULL;
      err = parse_alt(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        parser_state_restore(parser, &saved_state);
        return err;
      }

      nk_node_t* group_node = NULL;
      err = alloc_node_from_bytes(
        parser,
        NK_NODE_TYPE_GROUP,
        atom_span_bytes,
        node_span_end_bytes(parser, child_node),
        &group_node
      );
      if (err != NK_SUCCESS) {
        parser_state_restore(parser, &saved_state);
        nk_node_free(child_node);
        return err;
      }

      parser_state_restore(parser, &saved_state);

      group_node->group.child = child_node;
      group_node->group.has_name = false;
      group_node->group.group_num = 0;

      *out_node_ptr = group_node;

      // The next token is already lexed in `parse_alt`, so we return here
      // without lexing the next token again.
      return NK_SUCCESS;
    }
    case TK_CONDITIONAL_OPEN:
    {
      uint32_t group_num = tok->data.back_ref.group_num;
      bool has_name = tok->data.back_ref.has_name;
      nk_pbuf_t name_buf = tok->data.back_ref.name_buf;
      bool has_depth = tok->data.back_ref.has_depth;
      int32_t depth = tok->data.back_ref.depth;

      nk_error_t err = lex(parser, tok);
      if (err != NK_SUCCESS) {
        if (has_name) {
          nk_pbuf_free(&name_buf);
        }
        return err;
      }

      nk_node_t* yes_node;
      err = parse_concat(parser, tok, &yes_node);
      if (err != NK_SUCCESS) {
        if (has_name) {
          nk_pbuf_free(&name_buf);
        }
        return err;
      }

      nk_node_t* no_node = NULL;
      if (tok->type == TK_ALT) {
        nk_error_t err = lex(parser, tok);
        if (err != NK_SUCCESS) {
          nk_node_free(yes_node);
          if (has_name) {
            nk_pbuf_free(&name_buf);
          }
          return err;
        }

        err = parse_concat(parser, tok, &no_node);
        if (err != NK_SUCCESS) {
          nk_node_free(yes_node);
          if (has_name) {
            nk_pbuf_free(&name_buf);
          }
          return err;
        }
      }

      if (tok->type != TK_GROUP_CLOSE) {
        nk_node_free(yes_node);
        nk_node_free(no_node);
        if (has_name) {
          nk_pbuf_free(&name_buf);
        }
        set_error_span(parser, tok->span_bytes, tok->span_bytes);
        return NK_ERR_INVALID_CONDITIONAL_GROUP;
      }

      nk_node_t* conditional_node = NULL;
      err = alloc_node_from_bytes(
        parser,
        NK_NODE_TYPE_CONDITIONAL,
        atom_span_bytes,
        tok->span_bytes_end,
        &conditional_node
      );
      if (err != NK_SUCCESS) {
        nk_node_free(yes_node);
        nk_node_free(no_node);
        if (has_name) {
          nk_pbuf_free(&name_buf);
        }
        return err;
      }

      conditional_node->conditional.has_name = has_name;
      if (has_name) {
        conditional_node->conditional.name_buf = name_buf;
      }
      conditional_node->conditional.group_num = group_num;
      conditional_node->conditional.has_depth = has_depth;
      conditional_node->conditional.depth = depth;
      conditional_node->conditional.yes_child = yes_node;
      conditional_node->conditional.no_child = no_node;

      *out_node_ptr = conditional_node;
      break;
    }
    case TK_GROUP_CLOSE:
      return NK_ERR_UNMATCHED_CLOSE_PARENTHESIS;
    case TK_END:
      return NK_ERR_UNEXPECTED_END_OF_PATTERN;
    default:
      return NK_ERR_PARSER_BUG;
  }

  nk_error_t err = lex(parser, tok);
  if (err != NK_SUCCESS) {
    nk_node_free(*out_node_ptr);
    *out_node_ptr = NULL;
    return err;
  }

  return NK_SUCCESS;
}

static nk_error_t parse_quantifier_impl(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  nk_error_t err = parse_atom(parser, tok, out_node_ptr);
  if (err != NK_SUCCESS) {
    return err;
  }

  while (tok->type == TK_QUANTIFIER) {
    nk_node_t* quantifier_node = NULL;
    err = alloc_node_from_bytes(
      parser,
      NK_NODE_TYPE_QUANTIFIER,
      node_span_begin_bytes(parser, *out_node_ptr),
      tok->span_bytes_end,
      &quantifier_node
    );
    if (err != NK_SUCCESS) {
      nk_node_free(*out_node_ptr);
      *out_node_ptr = NULL;
      return err;
    }

    quantifier_node->quantifier.child = *out_node_ptr;
    quantifier_node->quantifier.min = tok->data.quantifier.min;
    quantifier_node->quantifier.max = tok->data.quantifier.max;
    quantifier_node->quantifier.type = tok->data.quantifier.type;

    *out_node_ptr = quantifier_node;

    err = lex(parser, tok);
    if (err != NK_SUCCESS) {
      nk_node_free(*out_node_ptr);
      *out_node_ptr = NULL;
      return err;
    }
  }

  return NK_SUCCESS;
}

static nk_error_t parse_concat_impl(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  if (tok->type == TK_ALT || tok->type == TK_GROUP_CLOSE || tok->type == TK_END) {
    nk_node_t* empty_node = NULL;
    nk_error_t err = alloc_node_from_bytes(parser, NK_NODE_TYPE_CONCAT, tok->span_bytes, tok->span_bytes, &empty_node);
    if (err != NK_SUCCESS) {
      return err;
    }
    empty_node->concat.children = NULL;
    empty_node->concat.children_len = 0;

    *out_node_ptr = empty_node;
    return NK_SUCCESS;
  }

  nk_error_t err = parse_quantifier(parser, tok, out_node_ptr);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (tok->type == TK_ALT || tok->type == TK_GROUP_CLOSE || tok->type == TK_END) {
    return NK_SUCCESS;
  }

  size_t concat_children_cap = 2;
  nk_node_t** concat_children = (nk_node_t**)malloc(sizeof(nk_node_t*) * concat_children_cap);
  if (concat_children == NULL) {
    nk_node_free(*out_node_ptr);
    *out_node_ptr = NULL;
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  bool last_child_is_string = (*out_node_ptr)->base.type == NK_NODE_TYPE_LITERAL;

  size_t concat_children_len = 0;
  concat_children[concat_children_len++] = *out_node_ptr;

  while (true) {
    if (concat_children_len >= concat_children_cap) {
      concat_children_cap *= 2;
      nk_node_t** new_concat_children = (nk_node_t**)realloc(concat_children, sizeof(nk_node_t*) * concat_children_cap);
      if (new_concat_children == NULL) {
        nodes_free(concat_children, concat_children_len);
        *out_node_ptr = NULL;
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      concat_children = new_concat_children;
    }

    concat_children[concat_children_len] = NULL;
    err = parse_quantifier(parser, tok, &concat_children[concat_children_len++]);
    if (err != NK_SUCCESS) {
      nodes_free(concat_children, concat_children_len - 1);
      *out_node_ptr = NULL;
      return err;
    }

    if (concat_children[concat_children_len - 1]->base.type == NK_NODE_TYPE_LITERAL) {
      if (last_child_is_string) {
        nk_node_t* last_literal_node = concat_children[concat_children_len - 2];
        nk_node_t* new_literal_node = concat_children[concat_children_len - 1];

        nk_error_t err = pbuf_append(&last_literal_node->literal.buf, &new_literal_node->literal.buf);
        if (err != NK_SUCCESS) {
          nodes_free(concat_children, concat_children_len);
          *out_node_ptr = NULL;
          return err;
        }

        set_node_span_from_bytes(
          parser,
          last_literal_node,
          node_span_begin_bytes(parser, last_literal_node),
          node_span_end_bytes(parser, new_literal_node)
        );

        nk_node_free(new_literal_node);
        concat_children[--concat_children_len] = NULL;
      }

      last_child_is_string = true;
    } else {
      last_child_is_string = false;
    }

    if (tok->type == TK_ALT || tok->type == TK_GROUP_CLOSE || tok->type == TK_END) {
      break;
    }
  }

  if (concat_children_len == 1) {
    *out_node_ptr = concat_children[0];
    free(concat_children);
    return NK_SUCCESS;
  }

  nk_node_t** resized_concat_children = (nk_node_t**)realloc(concat_children, sizeof(nk_node_t*) * concat_children_len);
  if (resized_concat_children == NULL) {
    nodes_free(concat_children, concat_children_len);
    *out_node_ptr = NULL;
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  concat_children = resized_concat_children;

  nk_node_t* concat_node = NULL;
  err = alloc_node_from_bytes(
    parser,
    NK_NODE_TYPE_CONCAT,
    node_span_begin_bytes(parser, concat_children[0]),
    node_span_end_bytes(parser, concat_children[concat_children_len - 1]),
    &concat_node
  );
  if (err != NK_SUCCESS) {
    nodes_free(concat_children, concat_children_len);
    *out_node_ptr = NULL;
    return err;
  }

  concat_node->concat.children = concat_children;
  concat_node->concat.children_len = concat_children_len;

  *out_node_ptr = concat_node;
  return NK_SUCCESS;
}

static nk_error_t parse_alt_impl(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  nk_error_t err = parse_concat(parser, tok, out_node_ptr);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (tok->type != TK_ALT) {
    return NK_SUCCESS;
  }

  size_t alt_children_cap = 2;
  nk_node_t** alt_children = (nk_node_t**)malloc(sizeof(nk_node_t*) * alt_children_cap);
  if (alt_children == NULL) {
    nk_node_free(*out_node_ptr);
    *out_node_ptr = NULL;
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  size_t alt_children_len = 0;
  alt_children[alt_children_len++] = *out_node_ptr;

  while (tok->type == TK_ALT) {
    err = lex(parser, tok);
    if (err != NK_SUCCESS) {
      nodes_free(alt_children, alt_children_len);
      *out_node_ptr = NULL;
      return err;
    }

    if (alt_children_len >= alt_children_cap) {
      alt_children_cap *= 2;
      nk_node_t** new_alt_children = (nk_node_t**)realloc(alt_children, sizeof(nk_node_t*) * alt_children_cap);
      if (new_alt_children == NULL) {
        nodes_free(alt_children, alt_children_len);
        *out_node_ptr = NULL;
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      alt_children = new_alt_children;
    }

    alt_children[alt_children_len] = NULL;
    err = parse_concat(parser, tok, &alt_children[alt_children_len++]);
    if (err != NK_SUCCESS) {
      nodes_free(alt_children, alt_children_len - 1);
      *out_node_ptr = NULL;
      return err;
    }
  }

  nk_node_t** resized_alt_children = (nk_node_t**)realloc(alt_children, sizeof(nk_node_t*) * alt_children_len);
  if (resized_alt_children == NULL) {
    nodes_free(alt_children, alt_children_len);
    *out_node_ptr = NULL;
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  alt_children = resized_alt_children;

  nk_node_t* alt_node = NULL;
  err = alloc_node_from_bytes(
    parser,
    NK_NODE_TYPE_ALT,
    node_span_begin_bytes(parser, alt_children[0]),
    node_span_end_bytes(parser, alt_children[alt_children_len - 1]),
    &alt_node
  );
  if (err != NK_SUCCESS) {
    nodes_free(alt_children, alt_children_len);
    *out_node_ptr = NULL;
    return err;
  }

  alt_node->alt.children = alt_children;
  alt_node->alt.children_len = alt_children_len;

  *out_node_ptr = alt_node;
  return NK_SUCCESS;
}

static nk_error_t parse_group_alt_body(nk_parser_t* parser, token_t* tok, nk_node_t** out_child_node_ptr) {
  nk_error_t err = enter_parse_depth(parser);
  if (err != NK_SUCCESS) {
    return err;
  }

  err = parse_group_alt_body_impl(parser, tok, out_child_node_ptr);
  leave_parse_depth(parser);
  return err;
}

static nk_error_t parse_atom(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  nk_error_t err = enter_parse_depth(parser);
  if (err != NK_SUCCESS) {
    return err;
  }

  err = parse_atom_impl(parser, tok, out_node_ptr);
  leave_parse_depth(parser);
  return err;
}

static nk_error_t parse_quantifier(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  nk_error_t err = enter_parse_depth(parser);
  if (err != NK_SUCCESS) {
    return err;
  }

  err = parse_quantifier_impl(parser, tok, out_node_ptr);
  leave_parse_depth(parser);
  return err;
}

static nk_error_t parse_concat(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  nk_error_t err = enter_parse_depth(parser);
  if (err != NK_SUCCESS) {
    return err;
  }

  err = parse_concat_impl(parser, tok, out_node_ptr);
  leave_parse_depth(parser);
  return err;
}

static nk_error_t parse_alt(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  nk_error_t err = enter_parse_depth(parser);
  if (err != NK_SUCCESS) {
    return err;
  }

  err = parse_alt_impl(parser, tok, out_node_ptr);
  leave_parse_depth(parser);
  return err;
}

nk_error_t nk_parser_parse(nk_parser_t* parser, nk_node_t** out_node_ptr) {
  *out_node_ptr = NULL;

  token_t tok;
  nk_error_t err = lex(parser, &tok);
  if (err != NK_SUCCESS) {
    return err;
  }

  err = parse_alt(parser, &tok, out_node_ptr);
  if (err != NK_SUCCESS) {
    if (parser->error_bytes == NULL) {
      parser->error_bytes = tok.span_bytes;
      parser->error_bytes_end = tok.span_bytes_end;
    }

    return err;
  }

  if (parser->in_unicode_escape_brace || tok.type != TK_END) {
    nk_node_free(*out_node_ptr);
    *out_node_ptr = NULL;

    if (parser->in_unicode_escape_brace) {
      return NK_ERR_UNCLOSED_UNICODE_ESCAPE_BRACE;
    }

    if (tok.type == TK_GROUP_CLOSE) {
      parser->error_bytes = tok.span_bytes;
      parser->error_bytes_end = tok.span_bytes_end;
      return NK_ERR_UNMATCHED_CLOSE_PARENTHESIS;
    }

    return NK_ERR_PARSER_BUG;
  }

  return NK_SUCCESS;
}
