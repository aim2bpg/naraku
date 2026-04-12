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

#define RANGE_QUANTIFIER_MAX_REPETITION 1000000
#define BARE_BACK_REF_MAX_NUM 10000
#define MAX_GROUP_NUM 10000000
#define BACK_REF_MAX_NUM 10000000
#define MAX_CAPTURE_DEPTH 1000

/**
 * Peeks at the next Unicode code point in the pattern buffer.
 *
 * Note that this functions assumes that the parser does not reach the end of
 * the pattern bytes. The caller should check that
 * `parser->pattern_bytes < parser->pattern_bytes_end` before calling this function.
 */
static inline nk_error_t peek(nk_parser_t* parser, int8_t* out_width, uint32_t* out_code) {
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

static nk_error_t
lex_decimal_number(nk_parser_t* parser, uint32_t* out_value, uint32_t max_value, nk_error_t overflow_error) {
  *out_value = 0;

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

    if (*out_value > (max_value - digit_value) / 10) {
      return overflow_error;
    }

    *out_value = *out_value * 10 + digit_value;
    parser->pattern_bytes += width;
  }

  return NK_SUCCESS;
}

static nk_error_t lex_bounded_quantifier(
  nk_parser_t* parser,
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
    nk_error_t err =
      lex_decimal_number(parser, out_min, RANGE_QUANTIFIER_MAX_REPETITION, NK_ERR_TOO_LARGE_NUMBER_IN_QUANTIFIER);
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
      nk_error_t err =
        lex_decimal_number(parser, out_max, RANGE_QUANTIFIER_MAX_REPETITION, NK_ERR_TOO_LARGE_NUMBER_IN_QUANTIFIER);
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

static nk_error_t lex_unicode_escape(nk_parser_t* parser, uint32_t* out_code, bool* out_is_unclosed_brace) {
  *out_code = 0;
  *out_is_unclosed_brace = false;

  if ((parser->enc->flags & NK_ENC_FLAG_UNICODE) == 0) {
    return NK_ERR_UNICODE_ESCAPE_IN_NON_UNICODE_ENCODING;
  }

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    return NK_ERR_UNCLOSED_UNICODE_ESCAPE_BRACE;
  }

  int8_t width;
  uint32_t code;
  nk_error_t err = peek(parser, &width, &code);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (is_hexdecimal_digit(code)) {
    nk_error_t err = lex_hexdecimal_number(parser, out_code, 4, 4, NK_ERR_INCOMPLETE_UNICODE_ESCAPE);
    if (err != NK_SUCCESS) {
      return err;
    }

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
      return NK_ERR_EMPTY_UNICODE_ESCAPE_BRACE;
    }

    err = lex_hexdecimal_number(parser, out_code, 1, 6, NK_ERR_INVALID_UNICODE_ESCAPE);
    if (err != NK_SUCCESS) {
      return err;
    }

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

  return NK_ERR_INCOMPLETE_UNICODE_ESCAPE;
}

static nk_error_t lex_unicode_escape_in_brace(nk_parser_t* parser, uint32_t* out_code, bool* out_is_unclosed_brace) {
  *out_code = 0;
  *out_is_unclosed_brace = true;

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
    return NK_ERR_EMPTY_UNICODE_ESCAPE_BRACE;
  }

  err = lex_hexdecimal_number(parser, out_code, 1, 6, NK_ERR_INVALID_UNICODE_ESCAPE);
  if (err != NK_SUCCESS) {
    return err;
  }
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

static nk_error_t lex_escape_single_byte(nk_parser_t* parser, uint8_t* out_byte) {
  bool retry = true;
  bool control_prefix = false;
  bool meta_prefix = false;

  while (retry) {
    retry = false;

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
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
          return NK_ERR_DUPLICATE_META_ESCAPE;
        }
        meta_prefix = true;
        if (parser->pattern_bytes >= parser->pattern_bytes_end) {
          return NK_ERR_INCOMPLETE_META_ESCAPE;
        }

        int8_t width;
        uint32_t code;
        nk_error_t err = consume(parser, &width, &code);
        if (err != NK_SUCCESS) {
          return err;
        }

        if (code != '-' || parser->pattern_bytes >= parser->pattern_bytes_end) {
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
          return NK_ERR_DUPLICATE_CONTROL_ESCAPE;
        }
        control_prefix = true;

        if (parser->pattern_bytes >= parser->pattern_bytes_end) {
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

static nk_error_t lex_escape_bytes(nk_parser_t* parser, uint32_t* out_code) {
  uint8_t first_byte;
  nk_error_t err = lex_escape_single_byte(parser, &first_byte);
  if (err != NK_SUCCESS) {
    return err;
  }

  uint8_t bytes[NK_ENC_MAX_MBC_WIDTH];
  bytes[0] = first_byte;

  int8_t width = nk_enc_scan_mbc_width(parser->enc, bytes, bytes + 1);
  if (width == 0) {
    return NK_ERR_INVALID_ESCAPED_BYTE_SEQUENCE;
  }

  if (width == 1) {
    *out_code = nk_enc_decode_mbc(parser->enc, bytes, bytes + 1);
    return NK_SUCCESS;
  }

  int8_t remaining_width = -width;
  for (int i = 1; i <= remaining_width; i++) {
    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      return NK_ERR_INCOMPLETE_ESCAPED_BYTE_SEQUENCE;
    }

    int8_t backslash_width;
    uint32_t backslash_code;
    nk_error_t err = peek(parser, &backslash_width, &backslash_code);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (backslash_code != '\\') {
      return NK_ERR_INCOMPLETE_ESCAPED_BYTE_SEQUENCE;
    }
    parser->pattern_bytes += backslash_width;  // consume `\`

    err = lex_escape_single_byte(parser, &bytes[i]);
    if (err != NK_SUCCESS) {
      return err;
    }
  }

  width = nk_enc_scan_mbc_width(parser->enc, bytes, bytes + 1 + remaining_width);
  if (width != 1 + remaining_width) {
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
        nk_error_t err = lex_unicode_escape(parser, &code, &u_is_unclosed_brace);
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

          err = lex_unicode_escape_in_brace(parser, &code, &u_is_unclosed_brace);
          if (err != NK_SUCCESS) {
            nk_pbuf_free(out_name_buf);
            return err;
          }
        }

        continue;
      }

      err = lex_escape_bytes(parser, &code);
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
  if (is_decimal_digit(code)) {
    *out_has_name = false;
  } else if (code == '-') {
    sign = -1;
    parser->pattern_bytes += width;  // consume `-`

    err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (!is_decimal_digit(code)) {
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
  err = lex_decimal_number(parser, &num, BACK_REF_MAX_NUM, NK_ERR_TOO_LARGE_GROUP_NUMBER);
  if (err != NK_SUCCESS) {
    return err;
  }

  int32_t num_or_relative_num = sign * (int32_t)num;
  if (num_or_relative_num >= 0) {
    *out_num = (uint32_t)num_or_relative_num;
  } else {
    if ((int64_t)parser->num_capture_groups + 1 + num_or_relative_num <= 0) {
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
    parser->pattern_bytes += width;  // consume `+` or `-`
    int sign = code == '+' ? 1 : -1;

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      if (*out_has_name) {
        nk_pbuf_free(out_name_buf);
      }
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
    err = lex_decimal_number(parser, &depth, MAX_CAPTURE_DEPTH, NK_ERR_TOO_LARGE_CAPTURE_DEPTH);
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

static nk_error_t lex_internal(nk_parser_t* parser, token_t* out_token) {
  out_token->span_bytes = parser->pattern_bytes;
  out_token->span_bytes_end = NULL;

  // Handles unclosed `\u{...` Unicode escapes that are not fully lexed in the previous
  // tokenization.
  if (parser->in_unicode_escape_brace) {
    uint32_t code;
    bool is_unclosed_brace;
    nk_error_t err = lex_unicode_escape_in_brace(parser, &code, &is_unclosed_brace);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (!is_unclosed_brace) {
      parser->in_unicode_escape_brace = false;
    }

    out_token->type = TK_CODE;
    out_token->data.code = code;
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
        if (parser->pattern_bytes < parser->pattern_bytes_end) {
          int8_t width;
          uint32_t code;
          nk_error_t err = peek(parser, &width, &code);
          if (err != NK_SUCCESS) {
            return err;
          }

          if (code == '?') {
            parser->pattern_bytes += width;  // consume `?`

            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
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
                  return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
                }

                int8_t lookbehind_width;
                uint32_t lookbehind_code;
                nk_error_t err = peek(parser, &lookbehind_width, &lookbehind_code);
                if (err != NK_SUCCESS) {
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
                  return NK_ERR_INVALID_GROUP_NAME;
                }

                err = peek(parser, &width, &code);
                if (err != NK_SUCCESS) {
                  nk_pbuf_free(&name_buf);
                  return err;
                }

                if (code != name_terminator) {
                  nk_pbuf_free(&name_buf);
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
                    return err;
                  }

                  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
                    if (has_name) {
                      nk_pbuf_free(&name_buf);
                    }
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
                    return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
                  }

                  parser->pattern_bytes += width;  // consume `'` or `>`
                } else {
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
                        return NK_ERR_UNDEFINED_GROUP_OPTION;
                      }
                      parser->pattern_bytes += width;
                      out_token->data.option.char_type_is_ascii_only = true;
                      out_token->data.option.posix_char_class_is_ascii_only = false;
                      break;
                    case 'a':
                      if (!is_positive) {
                        return NK_ERR_UNDEFINED_GROUP_OPTION;
                      }
                      parser->pattern_bytes += width;
                      out_token->data.option.char_type_is_ascii_only = true;
                      out_token->data.option.posix_char_class_is_ascii_only = true;
                      break;
                    case 'u':
                      if (!is_positive) {
                        return NK_ERR_UNDEFINED_GROUP_OPTION;
                      }
                      parser->pattern_bytes += width;
                      out_token->data.option.char_type_is_ascii_only = false;
                      out_token->data.option.posix_char_class_is_ascii_only = false;
                      break;
                    case 'S':
                      if (!is_positive) {
                        return NK_ERR_UNDEFINED_GROUP_OPTION;
                      }
                      parser->pattern_bytes += width;
                      out_token->data.option.fold_flags &= (nk_fold_flag_t)~NK_FOLD_FULL;
                      break;
                    case 'F':
                      if (!is_positive) {
                        return NK_ERR_UNDEFINED_GROUP_OPTION;
                      }
                      parser->pattern_bytes += width;
                      out_token->data.option.fold_flags |= NK_FOLD_FULL;
                      break;
                    case 'A':
                      if (!is_positive) {
                        return NK_ERR_UNDEFINED_GROUP_OPTION;
                      }
                      parser->pattern_bytes += width;
                      out_token->data.option.fold_flags |= NK_FOLD_ASCII_ONLY;
                      break;
                    case 'T':
                      if (!is_positive) {
                        return NK_ERR_UNDEFINED_GROUP_OPTION;
                      }
                      parser->pattern_bytes += width;
                      out_token->data.option.fold_flags |= NK_FOLD_TURKISH_AZERI;
                      break;
                    case '-':
                      if (!is_positive) {
                        // Onigmo allows redundant `-`, but we disallow it for simplicity.
                        return NK_ERR_UNDEFINED_GROUP_OPTION;
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
                      return NK_ERR_UNDEFINED_GROUP_OPTION;
                  }
                }

                return NK_ERR_INCOMPLETE_GROUP_SPECIFIER;
              }

              default:
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
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_DIGIT;
            out_token->data.char_type.is_positive = true;
            return NK_SUCCESS;
          case 'D':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_DIGIT;
            out_token->data.char_type.is_positive = false;
            return NK_SUCCESS;
          case 'w':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_WORD;
            out_token->data.char_type.is_positive = true;
            return NK_SUCCESS;
          case 'W':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_WORD;
            out_token->data.char_type.is_positive = false;
            return NK_SUCCESS;
          case 's':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_SPACE;
            out_token->data.char_type.is_positive = true;
            return NK_SUCCESS;
          case 'S':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_SPACE;
            out_token->data.char_type.is_positive = false;
            return NK_SUCCESS;
          case 'h':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_HEX_DIGIT;
            out_token->data.char_type.is_positive = true;
            return NK_SUCCESS;
          case 'H':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_HEX_DIGIT;
            out_token->data.char_type.is_positive = false;
            return NK_SUCCESS;

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
            bool is_positive = code == 'p';

            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              // TODO: add a warning for the incomplete character property escape.
              out_token->type = TK_CODE;
              out_token->data.code = code;  // 'p' or 'P'
              return NK_SUCCESS;
            }

            int8_t brace_width;
            uint32_t brace_code;
            nk_error_t err = peek(parser, &brace_width, &brace_code);
            if (err != NK_SUCCESS) {
              return err;
            }

            if (brace_code != '{') {
              // TODO: add a warning for the incomplete character property escape.
              out_token->type = TK_CODE;
              out_token->data.code = code;  // 'p' or 'P'
              return NK_SUCCESS;
            }

            parser->pattern_bytes += brace_width;  // consume `{`
            const uint8_t* name_bytes_for_error_report = parser->pattern_bytes;

            nk_pbuf_t name_buf;
            err = lex_name(parser, '}', false, &name_buf, NK_ERR_UNCLOSED_CHAR_PROP_ESCAPE_BRACE);
            if (err != NK_SUCCESS) {
              return err;
            }

            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              nk_pbuf_free(&name_buf);
              return NK_ERR_UNCLOSED_CHAR_PROP_ESCAPE_BRACE;
            }

            if (name_buf.bytes >= name_buf.bytes_end) {
              nk_pbuf_free(&name_buf);
              parser->error_bytes = name_bytes_for_error_report;
              parser->error_bytes_end = parser->pattern_bytes;
              return NK_ERR_EMPTY_CHAR_PROP_NAME;
            }

            err = consume(parser, &brace_width, &brace_code);
            if (err != NK_SUCCESS) {
              nk_pbuf_free(&name_buf);
              return err;
            }

            if (brace_code != '}') {
              nk_pbuf_free(&name_buf);
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

          // Named back-reference (`\k<name>`, `\k'name'`):
          case 'k':
          {
            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              // TODO: add a warning for the incomplete named back-reference escape.
              out_token->type = TK_CODE;
              out_token->data.code = 'k';
              return NK_SUCCESS;
            }

            int8_t next_width;
            uint32_t next_code;
            nk_error_t err = peek(parser, &next_width, &next_code);
            if (err != NK_SUCCESS) {
              return err;
            }

            if (next_code != '<' && next_code != '\'') {
              // TODO: add a warning for the incomplete named back-reference escape.
              out_token->type = TK_CODE;
              out_token->data.code = 'k';
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
              return err;
            }

            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              if (has_name) {
                nk_pbuf_free(&name_buf);
              }
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
            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              // TODO: add a warning for the incomplete sub-expression call escape.
              out_token->type = TK_CODE;
              out_token->data.code = 'g';
              return NK_SUCCESS;
            }

            int8_t next_width;
            uint32_t next_code;
            nk_error_t err = peek(parser, &next_width, &next_code);
            if (err != NK_SUCCESS) {
              return err;
            }

            if (next_code != '<' && next_code != '\'') {
              // TODO: add a warning for the incomplete sub-expression call escape.
              out_token->type = TK_CODE;
              out_token->data.code = 'g';
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
              return err;
            }

            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              if (has_name) {
                nk_pbuf_free(&name_buf);
              }
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

          // Unicode code point:
          case 'u':
          {
            uint32_t code;
            bool is_unclosed_brace;
            nk_error_t err = lex_unicode_escape(parser, &code, &is_unclosed_brace);
            if (err != NK_SUCCESS) {
              return err;
            }

            if (is_unclosed_brace) {
              parser->in_unicode_escape_brace = true;
            }

            out_token->type = TK_CODE;
            out_token->data.code = code;
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
            nk_error_t err = lex_decimal_number(parser, &num, BARE_BACK_REF_MAX_NUM, NK_ERR_INTERNAL_ERROR);
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

          // Other single-character escape sequences
          // (e.g., `\n`, `\t`, `\r`, `\f`, `\v`, `\a`, `\e`, etc.):
          case '0':
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
            nk_error_t err = lex_escape_bytes(parser, &escaped_code);
            if (err != NK_SUCCESS) {
              return err;
            }

            out_token->type = TK_CODE;
            out_token->data.code = escaped_code;
            return NK_SUCCESS;
          }

          case '\r':
          {
            bool is_crlf = false;
            if (parser->pattern_bytes < parser->pattern_bytes_end) {
              nk_error_t err = peek(parser, &width, &code);
              if (err != NK_SUCCESS) {
                return err;
              }

              is_crlf = code == '\n';
            }

            if (!is_crlf) {
              out_token->type = TK_CODE;
              out_token->data.code = '\r';
              return NK_SUCCESS;
            }

            parser->pattern_bytes += width;  // consume `\n` if it's CRLF
            FALLTHROUGH;
          }

          case '\n':
          {
            retry = true;
            continue;
          }

          // Other case: treat the escaped character as a code itself.
          default:
            out_token->type = TK_CODE;
            out_token->data.code = code;
            return NK_SUCCESS;
        }
      }
    }

    // TODO: add a warning for `]` outside of `[...]`

    out_token->type = TK_LITERAL;
    out_token->data.literal.bytes = parser->pattern_bytes - width;
    out_token->data.literal.bytes_end = parser->pattern_bytes;

    return NK_SUCCESS;
  }

  return NK_ERR_PARSER_BUG;  // unreachable
}

static inline nk_error_t lex(nk_parser_t* parser, token_t* out_token) {
  nk_error_t err = lex_internal(parser, out_token);
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

static nk_error_t lex_posix_char_class_name(nk_parser_t* parser, bool* out_is_closed, nk_pbuf_t* out_name_buf) {
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
      err = peek(parser, &width, &code);
      if (err != NK_SUCCESS) {
        nk_pbuf_free(out_name_buf);
        return err;
      }

      if (code == ']') {
        parser->pattern_bytes += width;  // consume `]`
        *out_is_closed = true;
        return NK_SUCCESS;
      }
    }

    if (code == '\\') {
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
        nk_error_t err = lex_unicode_escape(parser, &code, &u_is_unclosed_brace);
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

          err = lex_unicode_escape_in_brace(parser, &code, &u_is_unclosed_brace);
          if (err != NK_SUCCESS) {
            nk_pbuf_free(out_name_buf);
            return err;
          }
        }

        continue;
      }

      err = lex_escape_bytes(parser, &code);
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

static nk_error_t lex_in_char_class_internal(nk_parser_t* parser, token_t* out_token, lex_cc_state_t state) {
  out_token->span_bytes = parser->pattern_bytes;
  out_token->span_bytes_end = NULL;

  // Handles unclosed `\u{...` Unicode escapes that are not fully lexed in the previous
  // tokenization.
  if (parser->in_unicode_escape_brace) {
    uint32_t code;
    bool is_unclosed_brace;
    nk_error_t err = lex_unicode_escape_in_brace(parser, &code, &is_unclosed_brace);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (!is_unclosed_brace) {
      parser->in_unicode_escape_brace = false;
    }

    out_token->type = TK_CODE;
    out_token->data.code = code;
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

        int8_t next_width;
        uint32_t next_code;
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

          int8_t next_next_width;
          uint32_t next_next_code;
          nk_error_t err = peek(parser, &next_next_width, &next_next_code);
          if (err != NK_SUCCESS) {
            return err;
          }

          if (next_next_code == '&') {  // `&&`
            // `-&&` is not a range hyphen
            is_range_hyphen = false;
            parser->pattern_bytes -= next_width;  // put back `&`
          }
        }

        if (is_range_hyphen) {
          out_token->type = TK_CHAR_CLASS_RANGE_HYPHEN;
          return NK_SUCCESS;
        }

        if (!is_first && !is_last) {
          // TODO: add a warning for the literal `-`.
        }

        out_token->type = TK_CHAR_CLASS_LITERAL_HYPHEN;
        out_token->data.literal_hyphen.is_first = is_first;
        out_token->data.literal_hyphen.is_last = is_last;

        return NK_SUCCESS;
      }
      case '&':
      {
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
        int8_t next_width;
        uint32_t next_code;
        nk_error_t err = peek(parser, &next_width, &next_code);
        if (err != NK_SUCCESS) {
          return err;
        }

        if (next_code == ':') {
          const uint8_t* pattern_bytes_backup = parser->pattern_bytes;
          parser->pattern_bytes += next_width;  // consume `:`

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
              is_positive = false;
            }
          }

          bool is_closed = false;
          nk_pbuf_t name_buf;
          nk_error_t err = lex_posix_char_class_name(parser, &is_closed, &name_buf);
          if (err != NK_SUCCESS) {
            return err;
          }

          if (is_closed) {
            nk_posix_char_class_t char_class;
            nk_error_t err = name_to_posix_char_class(parser->enc, name_buf.bytes, name_buf.bytes_end, &char_class);
            if (err != NK_SUCCESS) {
              nk_pbuf_free(&name_buf);
              parser->error_bytes = pattern_bytes_backup;
              parser->error_bytes_end = parser->pattern_bytes;
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
        // TODO: add warning for the literal `]`.
        break;
      case '\\':
      {
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
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_DIGIT;
            out_token->data.char_type.is_positive = true;
            return NK_SUCCESS;
          case 'D':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_DIGIT;
            out_token->data.char_type.is_positive = false;
            return NK_SUCCESS;
          case 'w':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_WORD;
            out_token->data.char_type.is_positive = true;
            return NK_SUCCESS;
          case 'W':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_WORD;
            out_token->data.char_type.is_positive = false;
            return NK_SUCCESS;
          case 's':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_SPACE;
            out_token->data.char_type.is_positive = true;
            return NK_SUCCESS;
          case 'S':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_SPACE;
            out_token->data.char_type.is_positive = false;
            return NK_SUCCESS;
          case 'h':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_HEX_DIGIT;
            out_token->data.char_type.is_positive = true;
            return NK_SUCCESS;
          case 'H':
            out_token->type = TK_CHAR_TYPE;
            out_token->data.char_type.type = NK_CHAR_TYPE_HEX_DIGIT;
            out_token->data.char_type.is_positive = false;
            return NK_SUCCESS;

          // Character (Unicode) properties (`\p{...}` and `\P{...}`):
          case 'p':
          case 'P':
          {
            bool is_positive = code == 'p';

            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              // TODO: add a warning for the incomplete character property escape.
              out_token->type = TK_CODE;
              out_token->data.code = code;  // 'p' or 'P'
              return NK_SUCCESS;
            }

            int8_t brace_width;
            uint32_t brace_code;
            nk_error_t err = peek(parser, &brace_width, &brace_code);
            if (err != NK_SUCCESS) {
              return err;
            }

            if (brace_code != '{') {
              // TODO: add a warning for the incomplete character property escape.
              out_token->type = TK_CODE;
              out_token->data.code = code;  // 'p' or 'P'
              return NK_SUCCESS;
            }

            parser->pattern_bytes += brace_width;  // consume `{`
            const uint8_t* name_bytes_for_error_report = parser->pattern_bytes;

            nk_pbuf_t name_buf;
            err = lex_name(parser, '}', false, &name_buf, NK_ERR_UNCLOSED_CHAR_PROP_ESCAPE_BRACE);
            if (err != NK_SUCCESS) {
              return err;
            }

            if (parser->pattern_bytes >= parser->pattern_bytes_end) {
              nk_pbuf_free(&name_buf);
              return NK_ERR_UNCLOSED_CHAR_PROP_ESCAPE_BRACE;
            }

            if (name_buf.bytes >= name_buf.bytes_end) {
              nk_pbuf_free(&name_buf);
              parser->error_bytes = name_bytes_for_error_report;
              parser->error_bytes_end = parser->pattern_bytes;
              return NK_ERR_EMPTY_CHAR_PROP_NAME;
            }

            err = consume(parser, &brace_width, &brace_code);
            if (err != NK_SUCCESS) {
              nk_pbuf_free(&name_buf);
              return err;
            }

            if (brace_code != '}') {
              nk_pbuf_free(&name_buf);
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

          case 'u':
          {
            uint32_t code;
            nk_error_t err = lex_unicode_escape(parser, &code, &parser->in_unicode_escape_brace);
            if (err != NK_SUCCESS) {
              return err;
            }

            out_token->type = TK_CODE;
            out_token->data.code = code;
            return NK_SUCCESS;
          }

          // Other single-character escape sequences
          // (e.g., `\n`, `\t`, `\r`, `\f`, `\v`, `\a`, `\e`, etc.):
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
            nk_error_t err = lex_escape_bytes(parser, &escaped_code);
            if (err != NK_SUCCESS) {
              return err;
            }

            out_token->type = TK_CODE;
            out_token->data.code = escaped_code;
            return NK_SUCCESS;
          }

          case '\r':
          {
            bool is_crlf = false;
            if (parser->pattern_bytes < parser->pattern_bytes_end) {
              nk_error_t err = peek(parser, &width, &code);
              if (err != NK_SUCCESS) {
                return err;
              }

              is_crlf = code == '\n';
            }

            if (!is_crlf) {
              out_token->type = TK_CODE;
              out_token->data.code = '\r';
              return NK_SUCCESS;
            }

            parser->pattern_bytes += width;  // consume `\n` if it's CRLF
            FALLTHROUGH;
          }

          case '\n':
          {
            retry = true;
            continue;
          }

          // Other case: treat the escaped character as a code itself.
          default:
            out_token->type = TK_CODE;
            out_token->data.code = code;
            return NK_SUCCESS;
        }
      }
    }

    out_token->type = TK_CHAR_CLASS_LITERAL_CODE;
    out_token->data.code = code;

    return NK_SUCCESS;
  }

  return NK_ERR_PARSER_BUG;  // unreachable
}

static inline nk_error_t lex_in_char_class(nk_parser_t* parser, token_t* out_token, lex_cc_state_t state) {
  nk_error_t err = lex_in_char_class_internal(parser, out_token, state);
  if (err != NK_SUCCESS) {
    // If an error location is not set yet, we set it to the current position for better error
    // reporting.
    if (parser->error_bytes == NULL) {
      parser->error_bytes = parser->pattern_bytes;
      parser->error_bytes_end = parser->pattern_bytes;
    }

    return err;
  }

  return NK_SUCCESS;
}

// ==========================================================================
//
// Parser implementation:
//
// ==========================================================================

static nk_error_t parse_char_class_intersection(
  nk_parser_t* parser,
  bool* out_is_positive,
  size_t* out_unions_len,
  nk_char_class_union_t*** out_unions_ptr
);

static nk_error_t parse_char_class_item(nk_parser_t* parser, const token_t* tok, nk_char_class_item_t** out_item_ptr) {
  *out_item_ptr = NULL;

  nk_char_class_item_t* item = (nk_char_class_item_t*)malloc(sizeof(nk_char_class_item_t));
  if (item == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  switch (tok->type) {
    case TK_CODE:
    case TK_CHAR_CLASS_LITERAL_CODE:
    {
      item->type = NK_CHAR_CLASS_ITEM_TYPE_CODE;
      item->data.code = tok->data.code;
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
      bool is_positive = true;
      size_t unions_len = 0;
      nk_char_class_union_t** unions = NULL;
      nk_error_t err = parse_char_class_intersection(parser, &is_positive, &unions_len, &unions);
      if (err != NK_SUCCESS) {
        free(item);
        return err;
      }

      item->type = NK_CHAR_CLASS_ITEM_TYPE_NESTED_CHAR_CLASS;
      item->data.nested_char_class.is_positive = is_positive;
      item->data.nested_char_class.unions_len = unions_len;
      item->data.nested_char_class.unions = unions;
      *out_item_ptr = item;

      return NK_SUCCESS;
    }
    default:
      free(item);
      return NK_ERR_PARSER_BUG;
  }
}

static nk_error_t parse_char_class_union(nk_parser_t* parser, token_t* tok, nk_char_class_union_t** out_union_ptr) {
  *out_union_ptr = NULL;

  nk_char_class_union_t* u = (nk_char_class_union_t*)malloc(sizeof(nk_char_class_union_t));
  if (u == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  u->items_len = 0;
  u->items = NULL;

  if (tok->type == TK_CHAR_CLASS_CLOSE || tok->type == TK_CHAR_CLASS_INTERSECTION || tok->type == TK_END) {
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
        // TODO: add a warning for the literal `-` at the beginning of a character class.
        // Note that a warning for non-first literal `-` is already added by `lex_in_char_class`.
      }

      nk_error_t err = lex_in_char_class(parser, tok, CC_STATE_WAIT_RANGE_END);
      if (err != NK_SUCCESS) {
        char_class_union_free(u);
        char_class_item_free(begin_item);
        return err;
      }

      if (tok->type == TK_CHAR_CLASS_LITERAL_HYPHEN && tok->data.literal_hyphen.is_last) {
        // TODO: add a warning for the literal `-` at the end of a character class.
        // Note that a warning for non-last literal `-` is already added by `lex_in_char_class`.
      }

      if (begin_item->type != NK_CHAR_CLASS_ITEM_TYPE_CODE) {
        char_class_union_free(u);
        char_class_item_free(begin_item);
        // TODO: improve the error position.
        parser->error_bytes = begin_tok.span_bytes;
        parser->error_bytes_end = begin_tok.span_bytes_end;
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
        char_class_union_free(u);
        char_class_item_free(begin_item);
        char_class_item_free(end_item);
        // TODO: improve the error position.
        parser->error_bytes = tok->span_bytes;
        parser->error_bytes_end = tok->span_bytes;
        return NK_ERR_INVALID_CHAR_CLASS_RANGE;
      }

      if (begin_item->data.code > end_item->data.code) {
        char_class_union_free(u);
        char_class_item_free(begin_item);
        char_class_item_free(end_item);
        parser->error_bytes = begin_tok.span_bytes;
        parser->error_bytes_end = tok->span_bytes_end;
        return NK_ERR_CHAR_CLASS_RANGE_OUT_OF_ORDER;
      }

      uint32_t begin_code = begin_item->data.code;
      uint32_t end_code = end_item->data.code;
      char_class_item_free(end_item);

      begin_item->type = NK_CHAR_CLASS_ITEM_TYPE_RANGE;
      begin_item->data.range.begin_code = begin_code;
      begin_item->data.range.end_code = end_code;

      if (u->items_len >= items_cap) {
        size_t new_items_cap = items_cap * 2;
        nk_char_class_item_t** new_items =
          (nk_char_class_item_t**)realloc(u->items, sizeof(nk_char_class_item_t*) * new_items_cap);
        if (new_items == NULL) {
          char_class_union_free(u);
          char_class_item_free(begin_item);
          return NK_ERR_MEMORY_ALLOCATION_FAILED;
        }

        u->items = new_items;
        items_cap = new_items_cap;
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
      if (u->items_len >= items_cap) {
        size_t new_items_cap = items_cap * 2;
        nk_char_class_item_t** new_items =
          (nk_char_class_item_t**)realloc(u->items, sizeof(nk_char_class_item_t*) * new_items_cap);
        if (new_items == NULL) {
          char_class_union_free(u);
          char_class_item_free(begin_item);
          return NK_ERR_MEMORY_ALLOCATION_FAILED;
        }

        u->items = new_items;
        items_cap = new_items_cap;
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
    if (u->items_len >= items_cap) {
      size_t new_items_cap = items_cap * 2;
      nk_char_class_item_t** new_items =
        (nk_char_class_item_t**)realloc(u->items, sizeof(nk_char_class_item_t*) * new_items_cap);
      if (new_items == NULL) {
        char_class_union_free(u);
        char_class_item_free(begin_item);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }

      u->items = new_items;
      items_cap = new_items_cap;
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

  *out_union_ptr = u;

  return NK_SUCCESS;
}

static nk_error_t parse_char_class_intersection(
  nk_parser_t* parser,
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
    parser->error_bytes = tok.span_bytes;
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
      for (size_t i = 0; i < unions_len; i++) {
        char_class_union_free(unions[i]);
        unions[i] = NULL;
      }
      free(unions);
      return err;
    }

    nk_char_class_union_t* u = NULL;
    err = parse_char_class_union(parser, &tok, &u);
    if (err != NK_SUCCESS) {
      for (size_t i = 0; i < unions_len; i++) {
        char_class_union_free(unions[i]);
        unions[i] = NULL;
      }
      free(unions);
      return err;
    }

    if (unions_len >= unions_cap) {
      size_t new_unions_cap = unions_cap * 2;
      nk_char_class_union_t** new_unions =
        (nk_char_class_union_t**)realloc(unions, sizeof(nk_char_class_union_t*) * new_unions_cap);
      if (new_unions == NULL) {
        for (size_t i = 0; i < unions_len; i++) {
          char_class_union_free(unions[i]);
          unions[i] = NULL;
        }
        free(unions);
        char_class_union_free(u);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }

      unions = new_unions;
      unions_cap = new_unions_cap;
    }

    unions[unions_len++] = u;
  }

  if (unions_len < unions_cap) {
    nk_char_class_union_t** resized_unions =
      (nk_char_class_union_t**)realloc(unions, sizeof(nk_char_class_union_t*) * unions_len);
    if (resized_unions == NULL) {
      for (size_t i = 0; i < unions_len; i++) {
        char_class_union_free(unions[i]);
        unions[i] = NULL;
      }
      free(unions);
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    unions = resized_unions;
  }

  if (tok.type != TK_CHAR_CLASS_CLOSE) {
    for (size_t i = 0; i < unions_len; i++) {
      char_class_union_free(unions[i]);
      unions[i] = NULL;
    }
    free(unions);
    parser->error_bytes = tok.span_bytes;
    parser->error_bytes_end = tok.span_bytes_end;
    return NK_ERR_UNTERMINATED_CHAR_CLASS;
  }

  *out_unions_len = unions_len;
  *out_unions_ptr = unions;

  return NK_SUCCESS;
}

static nk_error_t parse_alt(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr);
static nk_error_t parse_concat(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr);

static nk_error_t parse_atom(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  switch (tok->type) {
    case TK_LITERAL:
    {
      nk_node_t* literal_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (literal_node == NULL) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      literal_node->base.type = NK_NODE_TYPE_LITERAL;
      literal_node->literal.buf =
        (nk_pbuf_t){.type = NK_PBUF_VIEW, .bytes = tok->data.literal.bytes, .bytes_end = tok->data.literal.bytes_end};
      literal_node->literal.is_ignore_case = parser->is_ignore_case;
      literal_node->literal.fold_flags = parser->fold_flags;
      *out_node_ptr = literal_node;
      break;
    }
    case TK_CODE:
    {
      nk_node_t* literal_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (literal_node == NULL) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      literal_node->base.type = NK_NODE_TYPE_LITERAL;

      uint32_t code = tok->data.code;
      uint8_t buf[NK_ENC_MAX_MBC_WIDTH];
      size_t width;
      nk_error_t err = nk_enc_encode_mbc(parser->enc, code, &width, buf);
      if (err != NK_SUCCESS) {
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
      nk_error_t err = parse_char_class_intersection(parser, &is_positive, &unions_len, &unions);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* char_class_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (char_class_node == NULL) {
        for (size_t i = 0; i < unions_len; i++) {
          char_class_union_free(unions[i]);
          unions[i] = NULL;
        }
        free(unions);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }

      char_class_node->base.type = NK_NODE_TYPE_CHAR_CLASS;
      char_class_node->char_class.is_positive = is_positive;
      char_class_node->char_class.unions_len = unions_len;
      char_class_node->char_class.unions = unions;
      *out_node_ptr = char_class_node;

      break;
    }
    case TK_DOT:
    {
      nk_node_t* dot_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (dot_node == NULL) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      dot_node->base.type = NK_NODE_TYPE_DOT;
      dot_node->dot.allows_newline = parser->dot_allows_newline;
      *out_node_ptr = dot_node;
      break;
    }
    case TK_ASSERTION:
    {
      nk_node_t* assertion_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (assertion_node == NULL) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      assertion_node->base.type = NK_NODE_TYPE_ASSERTION;
      assertion_node->assertion.type = tok->data.assertion.type;
      assertion_node->assertion.child = NULL;
      *out_node_ptr = assertion_node;
      break;
    }
    case TK_QUANTIFIER:
      return NK_ERR_NOTHING_TO_REPEAT;
    case TK_CHAR_TYPE:
    {
      nk_node_t* char_type_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (char_type_node == NULL) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      char_type_node->base.type = NK_NODE_TYPE_CHAR_TYPE;
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
      nk_node_t* char_prop_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (char_prop_node == NULL) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      char_prop_node->base.type = NK_NODE_TYPE_CHAR_PROP;
      char_prop_node->char_prop.cprop = tok->data.char_prop.cprop;
      char_prop_node->char_prop.is_positive = tok->data.char_prop.is_positive;
      char_prop_node->char_prop.is_ignore_case = parser->is_ignore_case;
      char_prop_node->char_prop.fold_flags = parser->fold_flags;
      *out_node_ptr = char_prop_node;
      break;
    }
    case TK_GRAPHEME_CLUSTER:
    {
      nk_node_t* gc_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (gc_node == NULL) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      gc_node->base.type = NK_NODE_TYPE_GRAPHEME_CLUSTER;
      *out_node_ptr = gc_node;
      break;
    }
    case TK_KEEP:
    {
      nk_node_t* keep_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (keep_node == NULL) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      keep_node->base.type = NK_NODE_TYPE_KEEP;
      *out_node_ptr = keep_node;
      break;
    }
    case TK_NEWLINE:
    {
      nk_node_t* newline_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (newline_node == NULL) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      newline_node->base.type = NK_NODE_TYPE_NEWLINE;
      *out_node_ptr = newline_node;
      break;
    }
    case TK_BACK_REF:
    {
      nk_node_t* back_ref_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (back_ref_node == NULL) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      back_ref_node->base.type = NK_NODE_TYPE_BACK_REF;
      back_ref_node->back_ref.has_name = tok->data.back_ref.has_name;
      back_ref_node->back_ref.group_num = tok->data.back_ref.group_num;
      back_ref_node->back_ref.has_depth = tok->data.back_ref.has_depth;
      back_ref_node->back_ref.depth = tok->data.back_ref.depth;
      if (tok->data.back_ref.has_name) {
        back_ref_node->back_ref.name_buf = tok->data.back_ref.name_buf;
      }
      *out_node_ptr = back_ref_node;
      break;
    }
    case TK_CALL:
    {
      nk_node_t* call_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (call_node == NULL) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      call_node->base.type = NK_NODE_TYPE_CALL;
      call_node->call.has_name = tok->data.call.has_name;
      call_node->call.group_num = tok->data.call.group_num;
      if (tok->data.call.has_name) {
        call_node->call.name_buf = tok->data.call.name_buf;
      }
      *out_node_ptr = call_node;
      break;
    }
    case TK_GROUP_OPEN:
    {
      parser->num_capture_groups++;
      if (parser->num_capture_groups > MAX_GROUP_NUM) {
        return NK_ERR_TOO_MANY_CAPTURE_GROUPS;
      }

      uint32_t group_num = parser->num_capture_groups;

      nk_error_t err = lex(parser, tok);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* child_node;
      err = parse_alt(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      if (tok->type != TK_GROUP_CLOSE) {
        nk_node_free(child_node);
        return NK_ERR_UNTERMINATED_GROUP;
      }

      nk_node_t* group_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (group_node == NULL) {
        nk_node_free(child_node);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }

      group_node->base.type = NK_NODE_TYPE_GROUP;
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
      if (parser->num_capture_groups > MAX_GROUP_NUM) {
        return NK_ERR_TOO_MANY_CAPTURE_GROUPS;
      }

      nk_pbuf_t name_buf = tok->data.named_group.name_buf;

      nk_error_t err = lex(parser, tok);
      if (err != NK_SUCCESS) {
        nk_pbuf_free(&name_buf);
        return err;
      }

      nk_node_t* child_node;
      err = parse_alt(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        nk_pbuf_free(&name_buf);
        return err;
      }

      if (tok->type != TK_GROUP_CLOSE) {
        nk_node_free(child_node);
        nk_pbuf_free(&name_buf);
        return NK_ERR_UNTERMINATED_GROUP;
      }

      nk_node_t* group_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (group_node == NULL) {
        nk_node_free(child_node);
        nk_pbuf_free(&name_buf);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }

      group_node->base.type = NK_NODE_TYPE_GROUP;
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
      nk_error_t err = lex(parser, tok);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* child_node;
      err = parse_alt(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      if (tok->type != TK_GROUP_CLOSE) {
        nk_node_free(child_node);
        return NK_ERR_UNTERMINATED_GROUP;
      }

      nk_node_t* lookaround_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (lookaround_node == NULL) {
        nk_node_free(child_node);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }

      lookaround_node->base.type = NK_NODE_TYPE_ASSERTION;
      lookaround_node->assertion.type = type;
      lookaround_node->assertion.child = child_node;
      *out_node_ptr = lookaround_node;
      break;
    }
    case TK_ATOMIC_OPEN:
    {
      nk_error_t err = lex(parser, tok);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* child_node;
      err = parse_alt(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      if (tok->type != TK_GROUP_CLOSE) {
        nk_node_free(child_node);
        return NK_ERR_UNTERMINATED_GROUP;
      }

      nk_node_t* atomic_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (atomic_node == NULL) {
        nk_node_free(child_node);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }

      atomic_node->base.type = NK_NODE_TYPE_ATOMIC;
      atomic_node->atomic.child = child_node;
      *out_node_ptr = atomic_node;
      break;
    }
    case TK_ABSENCE_OPEN:
    {
      nk_error_t err = lex(parser, tok);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* child_node;
      err = parse_alt(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      if (tok->type != TK_GROUP_CLOSE) {
        nk_node_free(child_node);
        return NK_ERR_UNTERMINATED_GROUP;
      }

      nk_node_t* absence_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (absence_node == NULL) {
        nk_node_free(child_node);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      absence_node->base.type = NK_NODE_TYPE_ABSENCE;
      absence_node->absence.child = child_node;
      *out_node_ptr = absence_node;
      break;
    }
    case TK_OPTION_GROUP_OPEN:
    {
      bool is_extended_mode = parser->is_extended_mode;
      bool is_ignore_case = parser->is_ignore_case;
      bool dot_allows_newline = parser->dot_allows_newline;
      bool char_class_is_strict = parser->char_class_is_strict;
      bool char_type_is_ascii_only = parser->char_type_is_ascii_only;
      bool posix_char_class_is_ascii_only = parser->posix_char_class_is_ascii_only;
      nk_fold_flag_t fold_flags = parser->fold_flags;

      parser->is_extended_mode = tok->data.option.is_extended_mode;
      parser->is_ignore_case = tok->data.option.is_ignore_case;
      parser->dot_allows_newline = tok->data.option.dot_allows_newline;
      parser->char_class_is_strict = tok->data.option.char_class_is_strict;
      parser->char_type_is_ascii_only = tok->data.option.char_type_is_ascii_only;
      parser->posix_char_class_is_ascii_only = tok->data.option.posix_char_class_is_ascii_only;
      parser->fold_flags = tok->data.option.fold_flags;

      nk_error_t err = lex(parser, tok);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* child_node;
      err = parse_alt(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      if (tok->type != TK_GROUP_CLOSE) {
        nk_node_free(child_node);
        return NK_ERR_UNTERMINATED_GROUP;
      }

      nk_node_t* group_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (group_node == NULL) {
        nk_node_free(child_node);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }

      group_node->base.type = NK_NODE_TYPE_GROUP;
      group_node->group.child = child_node;
      group_node->group.has_name = false;
      group_node->group.group_num = 0;

      parser->is_extended_mode = is_extended_mode;
      parser->is_ignore_case = is_ignore_case;
      parser->dot_allows_newline = dot_allows_newline;
      parser->char_class_is_strict = char_class_is_strict;
      parser->char_type_is_ascii_only = char_type_is_ascii_only;
      parser->posix_char_class_is_ascii_only = posix_char_class_is_ascii_only;
      parser->fold_flags = fold_flags;

      *out_node_ptr = group_node;
      break;
    }
    case TK_OPTION:
    {
      bool is_extended_mode = parser->is_extended_mode;
      bool is_ignore_case = parser->is_ignore_case;
      bool dot_allows_newline = parser->dot_allows_newline;
      bool char_class_is_strict = parser->char_class_is_strict;
      bool char_type_is_ascii_only = parser->char_type_is_ascii_only;
      bool posix_char_class_is_ascii_only = parser->posix_char_class_is_ascii_only;
      nk_fold_flag_t fold_flags = parser->fold_flags;

      parser->is_extended_mode = tok->data.option.is_extended_mode;
      parser->is_ignore_case = tok->data.option.is_ignore_case;
      parser->dot_allows_newline = tok->data.option.dot_allows_newline;
      parser->char_class_is_strict = tok->data.option.char_class_is_strict;
      parser->char_type_is_ascii_only = tok->data.option.char_type_is_ascii_only;
      parser->posix_char_class_is_ascii_only = tok->data.option.posix_char_class_is_ascii_only;
      parser->fold_flags = tok->data.option.fold_flags;

      nk_error_t err = lex(parser, tok);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* child_node;
      err = parse_alt(parser, tok, &child_node);
      if (err != NK_SUCCESS) {
        return err;
      }

      nk_node_t* group_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (group_node == NULL) {
        nk_node_free(child_node);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }

      group_node->base.type = NK_NODE_TYPE_GROUP;
      group_node->group.child = child_node;
      group_node->group.has_name = false;
      group_node->group.group_num = 0;

      parser->is_extended_mode = is_extended_mode;
      parser->is_ignore_case = is_ignore_case;
      parser->dot_allows_newline = dot_allows_newline;
      parser->char_class_is_strict = char_class_is_strict;
      parser->char_type_is_ascii_only = char_type_is_ascii_only;
      parser->posix_char_class_is_ascii_only = posix_char_class_is_ascii_only;
      parser->fold_flags = fold_flags;

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
        return NK_ERR_INVALID_CONDITIONAL_GROUP;
      }

      nk_node_t* conditional_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (conditional_node == NULL) {
        nk_node_free(yes_node);
        nk_node_free(no_node);
        if (has_name) {
          nk_pbuf_free(&name_buf);
        }
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }

      conditional_node->base.type = NK_NODE_TYPE_CONDITIONAL;
      conditional_node->conditional.has_name = has_name;
      conditional_node->conditional.group_num = group_num;
      if (has_name) {
        conditional_node->conditional.name_buf = name_buf;
      }
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

static nk_error_t parse_quantifier(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  nk_error_t err = parse_atom(parser, tok, out_node_ptr);
  if (err != NK_SUCCESS) {
    return err;
  }

  while (tok->type == TK_QUANTIFIER) {
    nk_node_t* quantifier_node = (nk_node_t*)malloc(sizeof(nk_node_t));
    if (quantifier_node == NULL) {
      nk_node_free(*out_node_ptr);
      *out_node_ptr = NULL;
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }

    quantifier_node->base.type = NK_NODE_TYPE_QUANTIFIER;
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

    // TODO: add a warning for nested quantifiers.
  }

  return NK_SUCCESS;
}

static nk_error_t parse_concat(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  if (tok->type == TK_ALT || tok->type == TK_GROUP_CLOSE || tok->type == TK_END) {
    nk_node_t* empty_node = (nk_node_t*)malloc(sizeof(nk_node_t));
    if (empty_node == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    empty_node->base.type = NK_NODE_TYPE_CONCAT;
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

  nk_node_t* concat_node = (nk_node_t*)malloc(sizeof(nk_node_t));
  if (concat_node == NULL) {
    nodes_free(concat_children, concat_children_len);
    *out_node_ptr = NULL;
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  concat_node->base.type = NK_NODE_TYPE_CONCAT;
  concat_node->concat.children = concat_children;
  concat_node->concat.children_len = concat_children_len;

  *out_node_ptr = concat_node;
  return NK_SUCCESS;
}

static nk_error_t parse_alt(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
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

  nk_node_t* alt_node = (nk_node_t*)malloc(sizeof(nk_node_t));
  if (alt_node == NULL) {
    nodes_free(alt_children, alt_children_len);
    *out_node_ptr = NULL;
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  alt_node->base.type = NK_NODE_TYPE_ALT;
  alt_node->alt.children = alt_children;
  alt_node->alt.children_len = alt_children_len;

  *out_node_ptr = alt_node;
  return NK_SUCCESS;
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
