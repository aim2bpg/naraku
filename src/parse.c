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
  out_parser->num_groups = 0;

  return NK_SUCCESS;
}

void nk_parser_free(nk_parser_t* parser ARG_UNUSED) {
}

// ==========================================================================
//
// Lexer implementation:
//
// ==========================================================================

#define RANGE_QUANTIFIER_MAX_REPETITION 100000
#define BACK_REF_MAX_NUM 1000

static inline nk_error_t peek(nk_parser_t* parser, int8_t* out_width, uint32_t* out_code) {
  int8_t width = nk_enc_scan_mbc_width(parser->enc, parser->pattern_bytes, parser->pattern_bytes_end);
  if (width <= 0) {
    return NK_ERR_INVALID_BYTE_SEQUENCE_IN_PATTERN;
  }

  *out_width = width;
  *out_code = nk_enc_decode_mbc(parser->enc, parser->pattern_bytes, parser->pattern_bytes_end);
  return NK_SUCCESS;
}

static inline nk_error_t next_code(nk_parser_t* parser, int8_t* out_width, uint32_t* out_code) {
  nk_error_t err = peek(parser, out_width, out_code);
  if (err != NK_SUCCESS) {
    return err;
  }

  parser->pattern_bytes += *out_width;
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

    parser->pattern_bytes += width;
  }
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

    if (code < '0' || code > '9') {
      break;
    }

    if (*out_value > (max_value - (code - '0')) / 10) {
      return overflow_error;
    }

    *out_value = *out_value * 10 + (code - '0');
    parser->pattern_bytes += width;
  }

  return NK_SUCCESS;
}

static nk_error_t lex_range_quantifier(
  nk_parser_t* parser,
  uint32_t* out_min,
  uint32_t* out_max,
  bool* out_is_incomplete,
  bool* out_allows_reluctant
) {
  const uint8_t* pattern_bytes_backup = parser->pattern_bytes;

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
      lex_decimal_number(parser, out_min, RANGE_QUANTIFIER_MAX_REPETITION, NK_ERR_TOO_BIG_NUMBER_IN_QUANTIFIER);
    if (err != NK_SUCCESS) {
      return err;
    }
    has_explicit_min = true;

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      parser->pattern_bytes = pattern_bytes_backup;
      return NK_SUCCESS;  // incomplete quantifier
    }

    err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }
  } else if (code == ',') {
    *out_min = 0;
  } else {
    parser->pattern_bytes = pattern_bytes_backup;
    return NK_SUCCESS;  // incomplete quantifier
  }

  if (code == ',') {                 // `{n,}` or `{n,m}`
    parser->pattern_bytes += width;  // consume `,`

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      parser->pattern_bytes = pattern_bytes_backup;
      return NK_SUCCESS;  // incomplete quantifier
    }

    nk_error_t err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }

    if ('0' <= code && code <= '9') {
      nk_error_t err =
        lex_decimal_number(parser, out_max, RANGE_QUANTIFIER_MAX_REPETITION, NK_ERR_TOO_BIG_NUMBER_IN_QUANTIFIER);
      if (err != NK_SUCCESS) {
        return err;
      }

      if (parser->pattern_bytes >= parser->pattern_bytes_end) {
        parser->pattern_bytes = pattern_bytes_backup;
        return NK_SUCCESS;  // incomplete quantifier
      }

      err = peek(parser, &width, &code);
      if (err != NK_SUCCESS) {
        return err;
      }
    } else {
      if (!has_explicit_min) {
        parser->pattern_bytes = pattern_bytes_backup;
        return NK_SUCCESS;  // incomplete_quantifier
      }
      *out_max = UINT32_MAX;
    }
  } else {  // `{n}`
    if (!has_explicit_min) {
      parser->pattern_bytes = pattern_bytes_backup;
      return NK_SUCCESS;  // incomplete quantifier
    }
    *out_allows_reluctant = false;
    *out_max = *out_min;
  }

  if (*out_min > *out_max) {
    return NK_ERR_NUMBERS_OUT_OF_ORDER_IN_QUANTIFIER;
  }

  if (code != '}') {
    parser->pattern_bytes = pattern_bytes_backup;
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
  nk_error_t too_short_error
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
    if ('0' <= code && code <= '9') {
      digit_value = code - '0';
    } else if ('A' <= code && code <= 'F') {
      digit_value = code - 'A' + 10;
    } else if ('a' <= code && code <= 'f') {
      digit_value = code - 'a' + 10;
    } else {
      break;
    }

    *out_code = (*out_code << 4) | digit_value;
    parser->pattern_bytes += width;
    digits++;
  }

  if (digits < min_digits) {
    return too_short_error;
  }

  return NK_SUCCESS;
}

static nk_error_t lex_unicode_code_point_escape(nk_parser_t* parser, uint32_t* out_code, bool* out_is_unclosed_brace) {
  *out_code = 0;
  *out_is_unclosed_brace = false;

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    return NK_ERR_INVALID_UNICODE_CODE_POINT_ESCAPE;
  }

  int8_t width;
  uint32_t code;
  nk_error_t err = peek(parser, &width, &code);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (('0' <= code && code <= '9') || ('A' <= code && code <= 'F') || ('a' <= code && code <= 'f')) {
    nk_error_t err = lex_hexdecimal_number(parser, out_code, 4, 4, NK_ERR_INVALID_UNICODE_CODE_POINT_ESCAPE);
    if (err != NK_SUCCESS) {
      return err;
    }

    return NK_SUCCESS;
  } else if (code == '{') {
    *out_is_unclosed_brace = true;
    parser->pattern_bytes += width;  // consume `{`

    skip_whitespace_in_unicode_brace(parser);
    nk_error_t err = lex_hexdecimal_number(parser, out_code, 1, 6, NK_ERR_INVALID_UNICODE_CODE_POINT_ESCAPE);
    if (err != NK_SUCCESS) {
      return err;
    }

    skip_whitespace_in_unicode_brace(parser);
    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      return NK_ERR_INVALID_UNICODE_CODE_POINT_ESCAPE;
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

  return NK_ERR_INVALID_UNICODE_CODE_POINT_ESCAPE;
}

static nk_error_t
lex_unicode_code_point_escape_in_brace(nk_parser_t* parser, uint32_t* out_code, bool* out_is_unclosed_brace) {
  *out_code = 0;
  *out_is_unclosed_brace = true;

  nk_error_t err = lex_hexdecimal_number(parser, out_code, 1, 6, NK_ERR_INVALID_UNICODE_CODE_POINT_ESCAPE);
  if (err != NK_SUCCESS) {
    return err;
  }
  skip_whitespace_in_unicode_brace(parser);

  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    return NK_ERR_INVALID_UNICODE_CODE_POINT_ESCAPE;
  }

  int8_t width;
  uint32_t code;
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
lex_octal_number(nk_parser_t* parser, uint32_t* out_code, int max_digits, nk_error_t too_short_error) {
  *out_code = 0;
  int digits = 0;

  while (parser->pattern_bytes < parser->pattern_bytes_end && digits < max_digits) {
    int8_t width;
    uint32_t code;
    nk_error_t err = peek(parser, &width, &code);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (code < '0' || code > '7') {
      break;
    }

    *out_code = (*out_code << 3) | (code - '0');
    parser->pattern_bytes += width;
    digits++;
  }

  if (digits == 0) {
    return too_short_error;
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
      return NK_ERR_TOO_SHORT_ESCAPE_SEQUENCE;
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

      case 'x':
      {
        parser->pattern_bytes += width;  // consume `x`

        uint32_t hex_code;
        nk_error_t err = lex_hexdecimal_number(parser, &hex_code, 1, 2, NK_ERR_TOO_SHORT_ESCAPE_SEQUENCE);
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
        nk_error_t err = lex_octal_number(parser, &octal_code, 3, NK_ERR_TOO_SHORT_ESCAPE_SEQUENCE);
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
          return NK_ERR_TOO_SHORT_META_ESCAPE;
        }

        int8_t width;
        uint32_t code;
        nk_error_t err = next_code(parser, &width, &code);
        if (err != NK_SUCCESS) {
          return err;
        }

        if (code != '-') {
          return NK_ERR_TOO_SHORT_META_ESCAPE;
        }

        if (parser->pattern_bytes >= parser->pattern_bytes_end) {
          return NK_ERR_TOO_SHORT_META_ESCAPE;
        }

        err = next_code(parser, &width, &code);
        if (err != NK_SUCCESS) {
          return err;
        }

        if (code == '\\') {
          retry = true;
          continue;
        }
        if (code > 0x7F) {
          return NK_ERR_TOO_SHORT_META_ESCAPE;
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
          return NK_ERR_TOO_SHORT_META_ESCAPE;
        }

        int8_t width;
        uint32_t code;
        nk_error_t err = next_code(parser, &width, &code);
        if (err != NK_SUCCESS) {
          return err;
        }

        if (needs_hyphen) {
          if (code != '-') {
            return NK_ERR_TOO_SHORT_CONTROL_ESCAPE;
          }

          if (parser->pattern_bytes >= parser->pattern_bytes_end) {
            return NK_ERR_TOO_SHORT_CONTROL_ESCAPE;
          }

          err = next_code(parser, &width, &code);
          if (err != NK_SUCCESS) {
            return err;
          }
        }

        if (code == '\\') {
          retry = true;
          continue;
        }
        if (code > 0x7F) {
          return NK_ERR_TOO_SHORT_CONTROL_ESCAPE;
        }
        *out_byte = (uint8_t)code;
        break;
      }

      default:
        return NK_ERR_UNEXPECTED_ESCAPE_SEQUENCE;
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
  if (parser->pattern_bytes >= parser->pattern_bytes_end) {
    return NK_ERR_TOO_SHORT_ESCAPE_SEQUENCE;
  }

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
      return NK_ERR_INCOMPLETE_ESCAPE_SEQUENCE;
    }

    int8_t backslash_width;
    uint32_t backslash_code;
    nk_error_t err = peek(parser, &backslash_width, &backslash_code);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (backslash_code != '\\') {
      return NK_ERR_INCOMPLETE_ESCAPE_SEQUENCE;
    }
    parser->pattern_bytes += backslash_width;  // consume `\`

    err = lex_escape_single_byte(parser, &bytes[i]);
    if (err != NK_SUCCESS) {
      return err;
    }

    width = nk_enc_scan_mbc_width(parser->enc, bytes, bytes + i + 1);
    if (width == 0) {
      return NK_ERR_INVALID_ESCAPED_BYTE_SEQUENCE;
    }
    if (width > 0) {
      break;
    }

    // remaining_width = (int8_t)(i + -width);
  }

  *out_code = nk_enc_decode_mbc(parser->enc, bytes, bytes - width);
  return NK_SUCCESS;
}

static nk_error_t lex(nk_parser_t* parser, token_t* out_token) {
  if (parser->in_unicode_escape_brace) {
    uint32_t code;
    bool is_unclosed_brace;
    nk_error_t err = lex_unicode_code_point_escape_in_brace(parser, &code, &is_unclosed_brace);
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

    if (parser->pattern_bytes >= parser->pattern_bytes_end) {
      out_token->type = TK_END;
      return NK_SUCCESS;
    }

    int8_t width;
    uint32_t code;
    nk_error_t err = next_code(parser, &width, &code);
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
            nk_error_t err = next_code(parser, &width, &code);
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
        out_token->type = TK_QUANTIFIER;

        bool is_incomplete;
        bool allows_reluctant;
        nk_error_t err = lex_range_quantifier(
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
          break;
        }

        err = lex_quantifier_type(parser, &out_token->data.quantifier.type, allows_reluctant);
        if (err != NK_SUCCESS) {
          return err;
        }
        return NK_SUCCESS;
      }

      case '\\':
      {
        if (parser->pattern_bytes >= parser->pattern_bytes_end) {
          return NK_ERR_TOO_SHORT_ESCAPE_SEQUENCE;
        }

        nk_error_t err = next_code(parser, &width, &code);
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

          // Unicode code point:
          case 'u':
          {
            uint32_t code;
            bool is_unclosed_brace;
            nk_error_t err = lex_unicode_code_point_escape(parser, &code, &is_unclosed_brace);
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

            uint32_t num;
            nk_error_t err = lex_decimal_number(parser, &num, BACK_REF_MAX_NUM, NK_ERR_INTERNAL_ERROR);
            if (err != NK_SUCCESS && err != NK_ERR_INTERNAL_ERROR) {
              return err;
            }

            if (err == NK_SUCCESS && (num <= 9 || num <= parser->num_capture_groups)) {
              out_token->type = TK_BACK_REF;
              out_token->data.back_ref.group_num = num;
              return NK_SUCCESS;
            }

            parser->pattern_bytes = pattern_bytes_backup;
          }
            FALLTHROUGH;

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

          // Other case: treat the escaped character as a literal.
          default:
            out_token->type = TK_LITERAL;
            out_token->data.literal.pattern_bytes = parser->pattern_bytes - width;
            out_token->data.literal.pattern_bytes_end = parser->pattern_bytes;
            return NK_SUCCESS;
        }
      }
    }

    out_token->type = TK_LITERAL;
    out_token->data.literal.pattern_bytes = parser->pattern_bytes - width;
    out_token->data.literal.pattern_bytes_end = parser->pattern_bytes;

    return NK_SUCCESS;
  }

  return NK_ERR_INTERNAL_ERROR;  // unreachable
}

// ==========================================================================
//
// Parser implementation:
//
// ==========================================================================

static nk_error_t parse_atom(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  switch (tok->type) {
    case TK_LITERAL:
    {
      nk_node_t* literal_node = (nk_node_t*)malloc(sizeof(nk_node_t));
      if (literal_node == NULL) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      literal_node->base.type = NK_NODE_TYPE_LITERAL;
      literal_node->literal.buf = (nk_pbuf_t){
        .type = NK_PBUF_VIEW,
        .bytes = tok->data.literal.pattern_bytes,
        .bytes_end = tok->data.literal.pattern_bytes_end
      };
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
    default:
      // TODO: Fix error code for this case
      return NK_ERR_INTERNAL_ERROR;
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

    // TODO: Handle nested quantifier warning
  }

  return NK_SUCCESS;
}

static nk_error_t parse_concat(nk_parser_t* parser, token_t* tok, nk_node_t** out_node_ptr) {
  if (tok->type == TK_ALT || tok->type == TK_PAREN_CLOSE || tok->type == TK_END) {
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

  if (tok->type == TK_ALT || tok->type == TK_PAREN_CLOSE || tok->type == TK_END) {
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

        nk_pbuf_t new_buf;
        nk_error_t err = pbuf_concat(&last_literal_node->literal.buf, &new_literal_node->literal.buf, &new_buf);
        if (err != NK_SUCCESS) {
          nodes_free(concat_children, concat_children_len);
          *out_node_ptr = NULL;
          return err;
        }

        nk_pbuf_free(&last_literal_node->literal.buf);
        last_literal_node->literal.buf = new_buf;

        nk_node_free(new_literal_node);
        concat_children[--concat_children_len] = NULL;
      }

      last_child_is_string = true;
    } else {
      last_child_is_string = false;
    }

    if (tok->type == TK_ALT || tok->type == TK_PAREN_CLOSE || tok->type == TK_END) {
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
    return err;
  }

  if (parser->in_unicode_escape_brace) {
    nk_node_free(*out_node_ptr);
    *out_node_ptr = NULL;
    return NK_ERR_INVALID_UNICODE_CODE_POINT_ESCAPE;
  }

  if (tok.type != TK_END) {
    nk_node_free(*out_node_ptr);
    *out_node_ptr = NULL;
    // FIXME: correct error code
    return NK_ERR_INTERNAL_ERROR;
  }

  return NK_SUCCESS;
}
