/**
 * @file naraku_syntax.h
 */

#ifndef NARAKU_SYNTAX_H
#define NARAKU_SYNTAX_H

#include <naraku_common.h>
#include <naraku_encoding.h>
#include <naraku_error.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
//
// Pattern buffer:
//
// ============================================================================

/**
 * Enumeration of pattern buffer types.
 */
typedef enum {
  NK_PBUF_VIEW,
  NK_PBUF_OWNED,
} nk_pbuf_type_t;

/**
 * Structure representing a pattern buffer.
 *
 * A pattern buffer is a contiguous sequence of bytes that represents a portion
 * of the regex pattern. It can either be a view into the original pattern
 * string (i.e., `NK_PBUF_VIEW`) or an owned buffer that has been allocated and
 * needs to be freed (i.e., `NK_PBUF_OWNED`). The `type` field indicates whether
 * the buffer is a view or owned, and the `bytes` and `bytes_end` fields
 * indicate the range of bytes in the buffer.
 */
typedef struct {
  nk_pbuf_type_t type;
  size_t cap;  // capacity of the buffer (only used for owned buffers)
  const uint8_t* bytes;
  const uint8_t* bytes_end;
} nk_pbuf_t;

// ============================================================================
//
// Node types:
//
// ============================================================================

// Forward declarations of typedefs.

typedef struct nk_node_base nk_node_base_t;
typedef struct nk_literal_node nk_literal_node_t;
typedef struct nk_char_class_node nk_char_class_node_t;
typedef struct nk_char_type_node nk_char_type_node_t;
typedef struct nk_char_prop_node nk_char_prop_node_t;
typedef struct nk_dot_node nk_dot_node_t;
typedef struct nk_newline_node nk_newline_node_t;
typedef struct nk_grapheme_cluster_node nk_grapheme_cluster_node_t;
typedef struct nk_keep_node nk_keep_node_t;
typedef struct nk_back_ref_node nk_back_ref_node_t;
typedef struct nk_call_node nk_call_node_t;
typedef struct nk_assertion_node nk_assertion_node_t;
typedef struct nk_quantifier_node nk_quantifier_node_t;
typedef struct nk_group_node nk_group_node_t;
typedef struct nk_atomic_node nk_atomic_node_t;
typedef struct nk_absence_node nk_absence_node_t;
typedef struct nk_conditional_node nk_conditional_node_t;
typedef struct nk_concat_node nk_concat_node_t;
typedef struct nk_alt_node nk_alt_node_t;
typedef union nk_node nk_node_t;

/**
 * Enumeration of node types.
 *
 * The `type` field in `nk_node_base_t` indicates the actual type of the node,
 * which determines which member of the `nk_node` union is valid.
 */
typedef enum {
  NK_NODE_TYPE_UNKNOWN = 0,
  NK_NODE_TYPE_LITERAL,
  NK_NODE_TYPE_CHAR_CLASS,
  NK_NODE_TYPE_CHAR_TYPE,
  NK_NODE_TYPE_CHAR_PROP,
  NK_NODE_TYPE_DOT,
  NK_NODE_TYPE_NEWLINE,
  NK_NODE_TYPE_GRAPHEME_CLUSTER,
  NK_NODE_TYPE_KEEP,
  NK_NODE_TYPE_BACK_REF,
  NK_NODE_TYPE_CALL,
  NK_NODE_TYPE_ASSERTION,
  NK_NODE_TYPE_QUANTIFIER,
  NK_NODE_TYPE_GROUP,
  NK_NODE_TYPE_ATOMIC,
  NK_NODE_TYPE_ABSENCE,
  NK_NODE_TYPE_CONDITIONAL,
  NK_NODE_TYPE_CONCAT,
  NK_NODE_TYPE_ALT
} nk_node_type_t;

/**
 * Base structure for all regex AST nodes.
 */
struct nk_node_base {
  nk_node_type_t type;
};

/**
 * Structure representing a literal node in the regex AST.
 */
struct nk_literal_node {
  nk_node_base_t base;
  nk_pbuf_t buf;
  bool is_ignore_case;
  nk_fold_flag_t fold_flags;
};

/**
 * Enumeration of character class item types.
 */
typedef enum {
  NK_CHAR_CLASS_ITEM_TYPE_CODE = 0,
  NK_CHAR_CLASS_ITEM_TYPE_RANGE,
  NK_CHAR_CLASS_ITEM_TYPE_CHAR_TYPE,
  NK_CHAR_CLASS_ITEM_TYPE_POSIX_CHAR_CLASS,
  NK_CHAR_CLASS_ITEM_TYPE_CHAR_PROP,
  NK_CHAR_CLASS_ITEM_TYPE_NESTED_CHAR_CLASS,
} nk_char_class_item_type_t;

/**
 * Enumeration of character types (e.g., `\w`, `\d`, `\s`, `\h`).
 */
typedef enum {
  NK_CHAR_TYPE_WORD = 0,   // `\w`, `\W`
  NK_CHAR_TYPE_DIGIT,      // `\d`, `\D`
  NK_CHAR_TYPE_SPACE,      // `\s`, `\S`
  NK_CHAR_TYPE_HEX_DIGIT,  // `\h`, `\H`
} nk_char_type_t;

/**
 * Enumeration of POSIX character classes (e.g., `[[:digit:]]`).
 */
typedef enum {
  NK_POSIX_CHAR_CLASS_ALNUM = 0,  // `[[:alnum:]]`
  NK_POSIX_CHAR_CLASS_ALPHA,      // `[[:alpha:]]`
  NK_POSIX_CHAR_CLASS_BLANK,      // `[[:blank:]]`
  NK_POSIX_CHAR_CLASS_CNTRL,      // `[[:cntrl:]]`
  NK_POSIX_CHAR_CLASS_DIGIT,      // `[[:digit:]]`
  NK_POSIX_CHAR_CLASS_GRAPH,      // `[[:graph:]]`
  NK_POSIX_CHAR_CLASS_LOWER,      // `[[:lower:]]`
  NK_POSIX_CHAR_CLASS_PRINT,      // `[[:print:]]`
  NK_POSIX_CHAR_CLASS_PUNCT,      // `[[:punct:]]`
  NK_POSIX_CHAR_CLASS_SPACE,      // `[[:space:]]`
  NK_POSIX_CHAR_CLASS_UPPER,      // `[[:upper:]]`
  NK_POSIX_CHAR_CLASS_XDIGIT,     // `[[:xdigit:]]`
  NK_POSIX_CHAR_CLASS_ASCII,      // `[[:ascii:]]`
  NK_POSIX_CHAR_CLASS_WORD,       // `[[:word:]]`
} nk_posix_char_class_t;

/**
 * Structure representing an item in a character class.
 */
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
      struct nk_char_class_union** unions;
    } nested_char_class;
  } data;
} nk_char_class_item_t;

/**
 * Structure representing a union of character class items.
 */
typedef struct nk_char_class_union {
  size_t items_len;
  nk_char_class_item_t** items;
} nk_char_class_union_t;

/**
 * Structure representing a character class node in the regex AST.
 */
struct nk_char_class_node {
  nk_node_base_t base;
  bool is_strict;
  bool is_ignore_case;
  nk_fold_flag_t fold_flags;
  bool is_positive;
  size_t unions_len;
  nk_char_class_union_t** unions;
};

/**
 * Structure representing a character type node in the regex AST
 * (e.g., `\w`, `\d`, `\s`, `\h`).
 */
struct nk_char_type_node {
  nk_node_base_t base;
  bool is_ignore_case;
  nk_fold_flag_t fold_flags;
  bool is_positive;
  bool is_ascii_only;
  nk_char_type_t char_type;
};

/**
 * Structure representing a character property node in the regex AST
 * (e.g., `\p{Lu}`, `\P{Lu}`).
 */
struct nk_char_prop_node {
  nk_node_base_t base;
  bool is_ignore_case;
  nk_fold_flag_t fold_flags;
  bool is_positive;
  nk_cprop_t cprop;
};

/**
 * Structure representing a dot node in the regex AST (i.e., `.`).
 */
struct nk_dot_node {
  nk_node_base_t base;
  bool allows_newline;
};

/**
 * Structure representing a newline node in the regex AST (i.e., `\R`).
 */
struct nk_newline_node {
  nk_node_base_t base;
};

/**
 * Structure representing a grapheme cluster node in the regex AST (i.e., `\X`).
 */
struct nk_grapheme_cluster_node {
  nk_node_base_t base;
};

/**
 * Structure representing a keep node in the regex AST (i.e., `\K`).
 */
struct nk_keep_node {
  nk_node_base_t base;
};

/**
 * Structure representing a back reference node in the regex AST (e.g., `\1`,
 * `\k<name>`).
 */
struct nk_back_ref_node {
  nk_node_base_t base;
  bool is_ignore_case;
  nk_fold_flag_t fold_flags;
  bool has_name;
  nk_pbuf_t name_buf;
  int32_t group_num;
  int32_t depth;
};

/**
 * A special value indicating that the back reference depth is not specified.
 */
#define NK_BACK_REF_DEPTH_NOT_SPECIFIED INT32_MAX

/**
 * Structure representing a call node in the regex AST (e.g., `\g<name>`).
 */
struct nk_call_node {
  nk_node_base_t base;
  bool has_name;
  nk_pbuf_t name_buf;
  int32_t group_num;
};

/**
 * Enumeration of assertion types (e.g., `^`, `$`, `\b`, `(?=...)`).
 */
typedef enum {
  NK_ASSERTION_TYPE_BEGIN_OF_LINE = 0,     // `^`
  NK_ASSERTION_TYPE_END_OF_LINE,           // `$`
  NK_ASSERTION_TYPE_BEGIN_OF_STRING,       // `\A`
  NK_ASSERTION_TYPE_END_OF_STRING_STRICT,  // `\z`
  NK_ASSERTION_TYPE_END_OF_STRING_LOOSE,   // `\Z`
  NK_ASSERTION_TYPE_BEGIN_OF_MATCHING,     // `\G`
  NK_ASSERTION_TYPE_WORD_BOUNDARY,         // `\b`
  NK_ASSERTION_TYPE_NON_WORD_BOUNDARY,     // `\B`
  NK_ASSERTION_TYPE_POSITIVE_LOOKAHEAD,    // `(?=...)`
  NK_ASSERTION_TYPE_NEGATIVE_LOOKAHEAD,    // `(?!...)`
  NK_ASSERTION_TYPE_POSITIVE_LOOKBEHIND,   // `(?<=...)`
  NK_ASSERTION_TYPE_NEGATIVE_LOOKBEHIND,   // `(?<!...)`
} nk_assertion_type_t;

/**
 * Structure representing an assertion node in the regex AST
 * (e.g., `^`, `$`, `\b`, `(?=...)`, `(?<!...)`).
 */
struct nk_assertion_node {
  nk_node_base_t base;
  nk_assertion_type_t type;
  nk_node_t* child;  // nullable, used for lookaround assertions
};

/**
 * Enumeration of quantifier types.
 */
typedef enum {
  NK_QUANTIFIER_TYPE_GREEDY = 0,  // e.g., `a*`
  NK_QUANTIFIER_TYPE_RELUCTANT,   // e.g., `a*?`
  NK_QUANTIFIER_TYPE_POSSESSIVE,  // e.g., `a*+`
} nk_quantifier_type_t;

/**
 * Structure representing a quantifier node in the regex AST (e.g., `a*`, `a+`,
 * `a?`).
 */
struct nk_quantifier_node {
  nk_node_base_t base;
  nk_node_t* child;
  uint32_t min;
  uint32_t max;  // `UINT32_MAX` means no upper limit (i.e., `{min,}`)
  nk_quantifier_type_t type;
};

/**
 * Structure representing a group node in the regex AST
 * (e.g., `(abc)`, `(?<name>abc)`, `(?:abc)`).
 */
struct nk_group_node {
  nk_node_base_t base;
  bool has_name;
  nk_pbuf_t name_buf;
  int32_t group_num;
  nk_node_t* child;
};

/**
 * Structure representing an atomic group node in the regex AST (e.g.,
 * `(?>abc)`).
 */
struct nk_atomic_node {
  nk_node_base_t base;
  nk_node_t* child;
};

/**
 * Structure representing an absence node in the regex AST (e.g., `(?~abc)`).
 */
struct nk_absence_node {
  nk_node_base_t base;
  nk_node_t* child;
};

/**
 * Structure representing a conditional node in the regex AST (e.g.,
 * `(?(condition)yes|no)`).
 */
struct nk_conditional_node {
  nk_node_base_t base;
  bool has_name;
  nk_pbuf_t name_buf;
  int32_t group_num;
  nk_node_t* yes_child;
  nk_node_t* no_child;  // nullable
};

/**
 * Structure representing a concatenation node in the regex AST (e.g., `abc`).
 */
struct nk_concat_node {
  nk_node_base_t base;
  size_t children_len;
  nk_node_t** children;
};

/**
 * Structure representing an alternation node in the regex AST (e.g., `a|b|c`).
 */
struct nk_alt_node {
  nk_node_base_t base;
  size_t children_len;
  nk_node_t** children;
};

/**
 * Type representing a regex AST node.
 */
union nk_node {
  nk_node_base_t base;
  nk_literal_node_t literal;
  nk_char_class_node_t char_class;
  nk_char_type_node_t char_type;
  nk_char_prop_node_t char_prop;
  nk_dot_node_t dot;
  nk_newline_node_t newline;
  nk_grapheme_cluster_node_t grapheme_cluster;
  nk_keep_node_t keep;
  nk_back_ref_node_t back_ref;
  nk_call_node_t call;
  nk_assertion_node_t assertion;
  nk_quantifier_node_t quantifier;
  nk_group_node_t group;
  nk_atomic_node_t atomic;
  nk_absence_node_t absence;
  nk_conditional_node_t conditional;
  nk_concat_node_t concat;
  nk_alt_node_t alt;
};

// ==========================================================================
//
// src/node.c
//
// ==========================================================================

/**
 * Releases the memory allocated for a pattern buffer if it is owned.
 */
NARAKU_EXPORTED_FUNCTION
void nk_pbuf_free(nk_pbuf_t* pbuf);

/**
 * Converts a pattern buffer from a view to an owned copy.
 *
 * If the buffer is already owned, this function does nothing.
 * If the buffer is a view, a copy of the data is allocated and the buffer
 * is updated to be owned.
 */
NARAKU_EXPORTED_FUNCTION
nk_error_t nk_pbuf_to_owned(nk_pbuf_t* pbuf);

/**
 * Releases the memory allocated for a regex AST node and its children
 * recursively.
 */
NARAKU_EXPORTED_FUNCTION
void nk_node_free(nk_node_t* node);

/**
 * Recursively converts all pattern buffers in the given node tree from views
 * to owned copies.
 *
 * This is useful when the node tree needs to outlive the original pattern
 * string (e.g., when the parser is freed but the AST is retained).
 */
NARAKU_EXPORTED_FUNCTION
nk_error_t nk_node_to_owned(nk_node_t* node);

// ==========================================================================
//
// src/parse.c
//
// ==========================================================================

/**
 * Type representing a regex parser.
 */
typedef struct nk_parser nk_parser_t;

/**
 * Type representing a warning function.
 */
typedef void (*nk_warning_func_t)(const char* message);

struct nk_parser {
  const nk_encoding_t* enc;
  const uint8_t* pattern_bytes_begin;
  const uint8_t* pattern_bytes_end;
  nk_warning_func_t warning_func;

  const uint8_t* pattern_bytes;

  bool is_extended_mode;
  bool is_ignore_case;
  bool dot_allows_newline;
  bool char_class_is_strict;
  bool char_type_is_ascii_only;
  bool posix_char_class_is_ascii_only;
  nk_fold_flag_t fold_flags;

  // Internal states:
  bool in_unicode_escape_brace;
  uint32_t num_capture_groups;
  bool has_named_groups;

  const uint8_t* error_bytes;
  const uint8_t* error_bytes_end;
};

/**
 * Structure representing the options for a regex parser.
 */
typedef struct {
  bool is_extended_mode;                // corresponds to `x`
  bool is_ignore_case;                  // corresponds to `i`
  bool dot_allows_newline;              // corresponds to `m`
  bool char_class_is_strict;            // corresponds to `v`
  bool char_type_is_ascii_only;         // corresponds to `a`, `d`, `u`
  bool posix_char_class_is_ascii_only;  // corresponds to `a`, `d`, `u`
  nk_fold_flag_t fold_flags;            // corresponds to `S`, `F`, `T`, `A`

  nk_warning_func_t warning_func;
} nk_parser_options_t;

/**
 * Initializes a regex parser with the given pattern and options.
 */
NARAKU_EXPORTED_FUNCTION
nk_error_t nk_parser_init(
  const nk_encoding_t* enc,
  const uint8_t* pattern_bytes,
  const uint8_t* pattern_bytes_end,
  nk_parser_options_t options,
  nk_parser_t* out_parser
);

/**
 * Parses the regex pattern and builds the AST.
 */
NARAKU_EXPORTED_FUNCTION
nk_error_t nk_parser_parse(nk_parser_t* parser, nk_node_t** out_node_ptr);

/**
 * Releases the memory allocated for the regex parser.
 */
NARAKU_EXPORTED_FUNCTION
void nk_parser_free(nk_parser_t* parser);

#ifdef __cplusplus
}
#endif

#endif  // NARAKU_SYNTAX_H
