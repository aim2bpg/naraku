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

/**
 * Releases the allocated memory for a character class union.
 */
void char_class_union_free(nk_char_class_union_t* u);

/**
 * Releases the allocated memory for a character class item.
 */
void char_class_item_free(nk_char_class_item_t* item);

// ==========================================================================
//
// src/parse.c
//
// ==========================================================================

typedef enum {
  TK_END,
  TK_LITERAL,            // a multibyte character literal
  TK_CODE,               // e.g., `\n`, `\t`, `\xHH`, `\uHHHH`, `\u{...}`
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
  TK_LOOKAROUND_OPEN,    // `(?=`, `(?!`, `(?<=`, `(?<!`
  TK_ATOMIC_OPEN,        // `(?>`
  TK_ABSENCE_OPEN,       // `(?~`
  TK_CONDITIONAL_OPEN,   // `(?(condition)`
  TK_GROUP_CLOSE,        // `)`
  TK_QUANTIFIER,         // e.g., `*`, `+`, `?`, `{m,n}`
  TK_ALT,                // `|`

  // Only available in character class context:
  TK_CHAR_CLASS_LITERAL_CODE,    // a multibyte character literal in a character class
  TK_CHAR_CLASS_NEGATION,        // `^` at the beginning of a character class
  TK_CHAR_CLASS_LITERAL_HYPHEN,  // `-` in a character class, which can be a literal
  TK_CHAR_CLASS_RANGE_HYPHEN,    // `-` in a character class, which indicates a range
  TK_CHAR_CLASS_CLOSE,           // `]`
  TK_CHAR_CLASS_INTERSECTION,    // `&&`
  TK_POSIX_CHAR_CLASS,           // e.g., `[:alnum:]`
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
    struct {
      uint32_t value;
      const uint8_t* code_bytes;
      const uint8_t* code_bytes_end;
    } code;
    struct {
      bool is_positive;
      nk_char_type_t type;
    } char_type;
    struct {
      bool is_positive;
      nk_cprop_t cprop;
    } char_prop;
    struct {
      nk_ref_target_kind_t target_kind;
      nk_pbuf_t name_buf;
      uint32_t capture_num;
      bool has_depth;
      int32_t depth;
    } back_ref;
    struct {
      nk_assertion_type_t type;
    } assertion;
    struct {
      uint32_t min;
      bool has_max;
      uint32_t max;
      nk_quantifier_type_t type;
    } quantifier;
    struct {
      bool is_extended_mode;                // corresponds to `x`
      bool is_ignore_case;                  // corresponds to `i`
      bool dot_allows_newline;              // corresponds to `m`
      bool char_class_is_strict;            // corresponds to `v`
      bool char_type_is_ascii_only;         // corresponds to `a`, `d`, `u`
      bool posix_char_class_is_ascii_only;  // corresponds to `a`, `d`, `u`
      nk_fold_flag_t fold_flags;            // corresponds to `S`, `F`, `T`, `A`
    } option;
    struct {
      nk_pbuf_t name_buf;
    } named_group;
    struct {
      nk_call_target_kind_t target_kind;
      nk_pbuf_t name_buf;
      uint32_t capture_num;
    } call;
    struct {
      bool is_first;
      bool is_last;
    } literal_hyphen;
    struct {
      bool is_positive;
      nk_posix_char_class_t char_class;
    } posix_char_class;
  } data;
} token_t;

#endif  // NARAKU_SYNTAX_INTERNAL_H
