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
 * Concatenates two pattern buffers and outputs the result in `out_buf`.
 */
nk_error_t pbuf_concat(const nk_pbuf_t* buf1, const nk_pbuf_t* buf2, nk_pbuf_t* out_buf);

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
  TK_STRING,
  TK_ESCAPE_CHAR,
  TK_CHAR_CLASS_OPEN,   // `[`
  TK_CHAR_PROP,         // e.g., `\p{Lu}`, `\P{Lu}`
  TK_CHAR_TYPE,         // e.g., `\d`, `\w`, `\s`, `\h`
  TK_DOT,               // `.`
  TK_NEWLINE,           // `\R`
  TK_GRAPHEME_CLUSTER,  // `\X`
  TK_KEEP,              // `\K`
  TK_BACK_REF,
  TK_CALL,
  TK_ASSERTION,        // e.g., `^`, `$`, `\b`
  TK_PAREN_OPEN,       // `(`
  TK_PAREN_CLOSE,      // `)`
  TK_LOOKAHEAD_OPEN,   // `(?=`, `(?!`
  TK_LOOKBEHIND_OPEN,  // `(?<=`, `(?<!`
  TK_QUANTIFIER,       // e.g., `*`, `+`, `?`, `{m,n}`
  TK_ALT,              // `|`

  // Only available in character class context:
  TK_CHAR_CLASS_CHAR,
  TK_CHAR_CLASS_RANGE_DASH,  // `-`
  TK_CHAR_CLASS_CLOSE,       // `]`
  TK_CHAR_CLASS_AND,         // `&&`
  TK_POSIX_CHAR_CLASS_OPEN,  // `[:`, `[:^`, `[^:`
} token_type_t;

typedef struct {
  token_type_t type;
  union {
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
