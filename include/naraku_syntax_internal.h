/**
 * @file naraku_syntax_internal.h
 */

#ifndef NARAKU_SYNTAX_INTERNAL_H
#define NARAKU_SYNTAX_INTERNAL_H

#include <naraku_encoding.h>
#include <naraku_syntax.h>

// ==========================================================================
//
// src/node.c
//
// ==========================================================================

/**
 * Appends the contents of `buf2` to `buf1`, modifying `buf1` in place.
 */
nk_error_t pbuf_append(nk_pbuf_t* buf1, const nk_pbuf_t* buf2);

/**
 * Encodes the given Unicode code point `code` into a multibyte sequence using
 * the specified encoding `enc`, and appends the resulting bytes to the pattern
 * buffer `buf`.
 */
nk_error_t pbuf_append_code(nk_pbuf_t* buf, const nk_encoding_t* enc, uint32_t code);

/**
 * Resizes the owned buffer in `buf` to fit its current length, if it is an owned buffer.
 */
nk_error_t pbuf_resize(nk_pbuf_t* buf);

/**
 * Releases the allocated memory for regex AST nodes in the array `nodes` of length `len`.
 */
void nodes_free(nk_node_t** nodes, size_t len);

// ==========================================================================
//
// src/parse.c
//
// ==========================================================================

typedef enum {
  TK_END,
  TK_LITERAL,
  TK_CODE,
  TK_CHAR_CLASS_OPEN,    // `[`
  TK_CHAR_PROP,          // e.g., `\p{Lu}`, `\P{Lu}`
  TK_CHAR_TYPE,          // e.g., `\d`, `\w`, `\s`, `\h`
  TK_DOT,                // `.`
  TK_NEWLINE,            // `\R`
  TK_GRAPHEME_CLUSTER,   // `\X`
  TK_KEEP,               // `\K`
  TK_BACK_REF,           // e.g., `\1`, `\k<name>`, `\k'name'`
  TK_CALL,               // e.g., `\g<name>`, `\g'name'`
  TK_ASSERTION,          // e.g., `^`, `$`, `\b`
  TK_GROUP_OPEN,         // `(`
  TK_OPTION_GROUP_OPEN,  // `(?imxvdauSFAT-imxv:`
  TK_OPTION,             // `(?imxvdauSFAT-imxv)`
  TK_NAMED_GROUP_OPEN,   // `(?<name>`, `(?'name'`
  TK_LOOKAHEAD_OPEN,     // `(?=`, `(?!`
  TK_LOOKBEHIND_OPEN,    // `(?<=`, `(?<!`
  TK_ATOMIC_OPEN,        // `(?>`
  TK_ABSENCE_OPEN,       // `(?~`
  TK_CONDITIONAL_OPEN,   // `(?(condition)`
  TK_GROUP_CLOSE,        // `)`
  TK_QUANTIFIER,         // e.g., `*`, `+`, `?`, `{m,n}`
  TK_ALT,                // `|`

  // Only available in character class context:
  TK_CHAR_CLASS_CHAR,
  TK_CHAR_CLASS_RANGE_DASH,  // `-`
  TK_CHAR_CLASS_CLOSE,       // `]`
  TK_CHAR_CLASS_AND,         // `&&`
  TK_POSIX_CHAR_CLASS_OPEN,  // `[:`, `[:^`, `[^:`
} token_type_t;

typedef struct {
  token_type_t type;
  const uint8_t* span_bytes;
  const uint8_t* span_bytes_end;
  union {
    struct {
      const uint8_t* bytes;
      const uint8_t* bytes_end;
    } literal;
    uint32_t code;
    struct {
      bool is_positive;
      nk_char_type_t type;
    } char_type;
    struct {
      bool is_positive;
      nk_cprop_t cprop;
    } char_prop;
    struct {
      bool has_name;
      nk_pbuf_t name_buf;
      uint32_t group_num;
    } back_ref;
    struct {
      nk_assertion_type_t type;
    } assertion;
    struct {
      uint32_t min;
      uint32_t max;
      nk_quantifier_type_t type;
    } quantifier;
  } data;
} token_t;

#endif  // NARAKU_SYNTAX_INTERNAL_H
