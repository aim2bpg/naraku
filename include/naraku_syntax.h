/**
 * @file naraku_syntax.h
 */

 #ifndef NARAKU_SYNTAX_H
 #define NARAKU_SYNTAX_H

#include <naraku_common.h>
#include <naraku_error.h>
#include <naraku_encoding.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
//
// Node types:
//
// ============================================================================

typedef struct nk_node nk_node_t;

typedef enum {
  NK_NODE_TYPE_UNKNOWN = 0,
  NK_NODE_TYPE_LITERAL,
  NK_NODE_TYPE_CHAR_CLASS,
  NK_NODE_TYPE_CHAR_TYPE,
  NK_NODE_TYPE_CHAR_PROP,
  NK_NODE_TYPE_ANY,
  NK_NODE_TYPE_NEWLINE,
  NK_NODE_TYPE_GRAPHEME_CLUSTER,
  NK_NODE_TYPE_BACK_REF,
  NK_NODE_TYPE_CALL,
  NK_NODE_TYPE_ASSERTION,
  NK_NODE_TYPE_QUANTIFIER,
  NK_NODE_TYPE_GROUP,
  NK_NODE_TYPE_CONDITIONAL,
  NK_NODE_TYPE_CONCAT,
  NK_NODE_TYPE_ALT
} nk_node_type_t;

typedef struct nk_node_base {
  nk_node_type_t type;
} nk_node_base_t;

typedef struct nk_literal_node {
  nk_node_base_t base;
  const uint8_t* bytes;
  size_t bytes_len;
  nk_fold_flag_t fold_flags;
} nk_literal_node_t;

typedef enum {
  NK_CHAR_CLASS_ITEM_TYPE_CODE = 0,
  NK_CHAR_CLASS_ITEM_TYPE_RANGE,
  NK_CHAR_CLASS_ITEM_TYPE_CHAR_TYPE,
  NK_CHAR_CLASS_ITEM_TYPE_POSIX_CHAR_CLASS,
  NK_CHAR_CLASS_ITEM_TYPE_CHAR_PROP,
} nk_char_class_item_type_t;

typedef enum {
  NK_CHAR_TYPE_WORD = 0,  // `\w`, `\W`
  NK_CHAR_TYPE_DIGIT,     // `\d`, `\D`
  NK_CHAR_TYPE_SPACE,     // `\s`, `\S`
  NK_CHAR_TYPE_HEX_DIGIT, // `\h`, `\H`
} nk_char_type_t;

typedef enum {
  NK_POSIX_CHAR_CLASS_ALNUM = 0, // `[[:alnum:]]`
  NK_POSIX_CHAR_CLASS_ALPHA,     // `[[:alpha:]]`
  NK_POSIX_CHAR_CLASS_BLANK,     // `[[:blank:]]`
  NK_POSIX_CHAR_CLASS_CNTRL,     // `[[:cntrl:]]`
  NK_POSIX_CHAR_CLASS_DIGIT,     // `[[:digit:]]`
  NK_POSIX_CHAR_CLASS_GRAPH,     // `[[:graph:]]`
  NK_POSIX_CHAR_CLASS_LOWER,     // `[[:lower:]]`
  NK_POSIX_CHAR_CLASS_PRINT,     // `[[:print:]]`
  NK_POSIX_CHAR_CLASS_PUNCT,     // `[[:punct:]]`
  NK_POSIX_CHAR_CLASS_SPACE,     // `[[:space:]]`
  NK_POSIX_CHAR_CLASS_UPPER,     // `[[:upper:]]`
  NK_POSIX_CHAR_CLASS_XDIGIT,    // `[[:xdigit:]]`
  NK_POSIX_CHAR_CLASS_ASCII,     // `[[:ascii:]]`
  NK_POSIX_CHAR_CLASS_WORD,      // `[[:word:]]`
} nk_posix_char_class_t;

typedef struct nk_char_class_item {
  nk_char_class_item_type_t type;
  union {
    // e.g., `[a]`
    uint32_t code;
    // e.g., `[a-z]`
    struct {
      uint32_t from_code;
      uint32_t to_code;
    } range;
    // e.g., `\w`, `\d`
    struct {
      bool is_positive;
      bool is_ascii_only;
      nk_char_type_t char_type;
    } char_type;
    // e.g., `[[:digit:]]`, `[[:^digit:]]`
    struct {
      bool is_positive;
      bool is_ascii_only;
      nk_posix_char_class_t posix_char_class;
    } posix_char_class;
    // e.g., `\p{Lu}`, `\P{Lu}`
    struct {
      bool is_positive;
      nk_cprop_t cprop;
    } char_prop;
    // e.g., `[[a-z&&[^aeiou]]` (a nested character class)
    struct {
      bool is_positive;
      size_t unions_len;
      struct nk_char_class_union head_union;
      struct nk_char_class_union *tail_unions;
    } nested_char_class;
  } data;
} nk_char_class_item_t;

#define NK_CHAR_CLASS_ITEMS_CAPACITY 4

typedef struct nk_char_class_union {
  size_t items_len;
  nk_char_class_item_t inline_items[NK_CHAR_CLASS_ITEMS_CAPACITY];
  nk_char_class_item_t* dynamic_items;
} nk_char_class_union_t;

typedef struct nk_char_class_node {
  nk_node_base_t base;
  nk_fold_flag_t fold_flags;
  bool is_positive;
  size_t unions_len;
  nk_char_class_union_t head_union;
  nk_char_class_union_t *tail_unions;
} nk_char_class_node_t;

typedef struct nk_char_type_node {
  nk_node_base_t base;
  nk_fold_flag_t fold_flags;
  bool is_positive;
  bool is_ascii_only;
  nk_char_type_t char_type;
} nk_char_type_node_t;

typedef struct nk_char_prop_node {
  nk_node_base_t base;
  nk_fold_flag_t fold_flags;
  bool is_positive;
  nk_cprop_t cprop;
} nk_char_prop_node_t;

typedef struct nk_any_node {
  nk_node_base_t base;
  bool allow_newline;
} nk_any_node_t;

typedef struct nk_newline_node {
  nk_node_base_t base;
} nk_newline_node_t;

typedef struct nk_grapheme_cluster_node {
  nk_node_base_t base;
} nk_grapheme_cluster_node_t;

typedef struct nk_back_ref_node {
  nk_node_base_t base;
  uint8_t* name_bytes;
  uint8_t* name_bytes_end;
  uint32_t group_num;
  int32_t depth; // `INT32_MAX` means that `depth` is not specified.
} nk_back_ref_node_t;

typedef struct nk_call_node {
  nk_node_base_t base;
  uint8_t* name_bytes;
  uint8_t* name_bytes_end;
  uint32_t group_num;
} nk_call_node_t;

typedef enum {
  NK_ASSERTION_TYPE_BEGINNING_OF_LINE = 0, // `^`
  NK_ASSERTION_TYPE_END_OF_LINE,           // `$`
  NK_ASSERTION_TYPE_BEGINNING_OF_STRING,   // `\A`
  NK_ASSERTION_TYPE_END_OF_STRING_STRICT,  // `\z`
  NK_ASSERTION_TYPE_END_OF_STRING_LOOSE,   // `\Z`
  NK_ASSERTION_TYPE_BEGINNING_OF_MATCHING, // `\G`
  NK_ASSERTION_TYPE_WORD_BOUNDARY,         // `\b`
  NK_ASSERTION_TYPE_NON_WORD_BOUNDARY,     // `\B`
  NK_ASSERTION_TYPE_POSITIVE_LOOKAHEAD,    // `(?=...)`
  NK_ASSERTION_TYPE_NEGATIVE_LOOKAHEAD,    // `(?!...)`
  NK_ASSERTION_TYPE_POSITIVE_LOOKBEHIND,   // `(?<=...)`
  NK_ASSERTION_TYPE_NEGATIVE_LOOKBEHIND,   // `(?<!...)`
} nk_assertion_type_t;

typedef struct nk_assertion_node {
  nk_node_base_t base;
  nk_assertion_type_t type;
  nk_node_t* child;
} nk_assertion_node_t;

typedef enum {
  NK_QUANTIFIER_TYPE_GREEDY = 0, // e.g., `a*`
  NK_QUANTIFIER_TYPE_RELUCTANT,  // e.g., `a*?`
  NK_QUANTIFIER_TYPE_POSSESSIVE, // e.g., `a*+`
} nk_quantifier_type_t;

typedef struct nk_quantifier_node {
  nk_node_base_t base;
  nk_node_t* child;
  uint32_t min;
  uint32_t max; // `UINT32_MAX` means no upper limit (i.e., `{min,}`)
  nk_quantifier_type_t type;
} nk_quantifier_node_t;

#ifdef __cplusplus
}
#endif

#endif // NARAKU_SYNTAX_H
