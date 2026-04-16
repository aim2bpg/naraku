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
typedef struct nk_capture_node nk_capture_node_t;
typedef struct nk_group_node nk_group_node_t;
typedef struct nk_atomic_node nk_atomic_node_t;
typedef struct nk_absence_node nk_absence_node_t;
typedef struct nk_conditional_node nk_conditional_node_t;
typedef struct nk_concat_node nk_concat_node_t;
typedef struct nk_alt_node nk_alt_node_t;
typedef struct nk_capture_names_map nk_capture_names_map_t;
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
  NK_NODE_TYPE_CAPTURE,
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
  nk_node_type_t type;  // indicates the actual type of the node
  size_t span_offset;   // byte offset from the beginning of the pattern
  size_t span_length;   // byte length of this node in the pattern
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
  nk_char_class_item_type_t type;  // indicates the actual type of the item
  size_t span_offset;              // byte offset from the beginning of the pattern
  size_t span_length;              // byte length of this item in the pattern
  union {
    // e.g., `[a]`
    uint32_t code;
    // e.g., `[a-z]`
    struct {
      uint32_t begin_code;
      uint32_t end_code;
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
  size_t span_offset;  // byte offset from the beginning of the pattern
  size_t span_length;  // byte length of this union in the pattern
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
 * Enumeration of reference target kinds used by back references and
 * conditionals.
 */
typedef enum {
  NK_REF_TARGET_KIND_CAPTURE_NUM = 0,  // numeric target (e.g., `\1`, `(?(1)...)`)
  NK_REF_TARGET_KIND_NAME,             // named target (e.g., `\k<name>`, `(?(<name>)...)`)
} nk_ref_target_kind_t;

/**
 * Structure representing a back reference node in the regex AST (e.g., `\1`,
 * `\k<name>`).
 */
struct nk_back_ref_node {
  nk_node_base_t base;
  bool is_ignore_case;
  nk_fold_flag_t fold_flags;
  nk_ref_target_kind_t target_kind;
  nk_pbuf_t name_buf;    // valid when `target_kind == NK_REF_TARGET_KIND_NAME`
  uint32_t capture_num;  // valid (`capture_num > 0`) when `target_kind == NK_REF_TARGET_KIND_CAPTURE_NUM`
  size_t resolved_capture_name_map_entry_index;
  size_t resolved_capture_num_count;
  bool has_depth;
  int32_t depth;  // valid when `has_depth == true`
};

/**
 * A special value indicating that the back reference depth is not specified.
 */
#define NK_BACK_REF_DEPTH_NOT_SPECIFIED INT32_MAX

/**
 * Structure representing a call node in the regex AST (e.g., `\g<name>`).
 */
typedef enum {
  NK_CALL_TARGET_KIND_ROOT = 0,     // `\g<0>`
  NK_CALL_TARGET_KIND_CAPTURE_NUM,  // `\g<1>`
  NK_CALL_TARGET_KIND_NAME,         // `\g<name>`
} nk_call_target_kind_t;

struct nk_call_node {
  nk_node_base_t base;
  nk_call_target_kind_t target_kind;
  nk_pbuf_t name_buf;             // valid when `target_kind == NK_CALL_TARGET_KIND_NAME`
  uint32_t capture_num;           // valid (`capture_num > 0`) when `target_kind == NK_CALL_TARGET_KIND_CAPTURE_NUM`
  uint32_t resolved_capture_num;  // resolved target; `0` means root (`target_kind == NK_CALL_TARGET_KIND_ROOT`)
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
  bool has_max;
  uint32_t max;  // only meaningful when `has_max == true`
  nk_quantifier_type_t type;
};

/**
 * Structure representing a capturing group node in the regex AST
 * (e.g., `(abc)`, `(?<name>abc)`).
 */
struct nk_capture_node {
  nk_node_base_t base;
  bool has_name;
  nk_pbuf_t name_buf;    // valid when `has_name == true`
  uint32_t capture_num;  // capture number (`capture_num > 0`) for numbered captures; otherwise 0
  nk_node_t* child;
};

/**
 * Structure representing a non-capturing group node in the regex AST
 * (e.g., `(?:abc)`, `(?imx:abc)`, `(?imx)abc`).
 */
struct nk_group_node {
  nk_node_base_t base;
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
  nk_ref_target_kind_t target_kind;
  nk_pbuf_t name_buf;    // valid when `target_kind == NK_REF_TARGET_KIND_NAME`
  uint32_t capture_num;  // valid (`capture_num > 0`) when `target_kind == NK_REF_TARGET_KIND_CAPTURE_NUM`
  size_t resolved_capture_name_map_entry_index;
  size_t resolved_capture_num_count;
  bool has_depth;
  int32_t depth;  // valid when `has_depth == true`
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
  nk_capture_node_t capture;
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
typedef void (*nk_warning_func_t)(const nk_parser_t* parser, nk_warning_t warning, size_t offset, size_t length);

typedef struct {
  uint32_t* capture_nums;
  size_t capture_nums_len;
  size_t capture_nums_cap;
} nk_capture_entry_t;

#define NK_CAPTURE_NAME_MAP_ENTRY_INDEX_UNRESOLVED SIZE_MAX

#define NK_DEFAULT_RANGE_QUANTIFIER_MAX_REPETITION 1000000u
#define NK_DEFAULT_BARE_BACK_REF_MAX_NUM 10000u
#define NK_DEFAULT_MAX_CAPTURE_NUM 10000000u
#define NK_DEFAULT_BACK_REF_MAX_NUM 10000000u
#define NK_DEFAULT_MAX_CAPTURE_DEPTH 1000u
#define NK_DEFAULT_MAX_PARSE_DEPTH 1000u

struct nk_parser {
  const nk_encoding_t* enc;
  const uint8_t* pattern_bytes_begin;
  const uint8_t* pattern_bytes_end;
  nk_warning_func_t warning_func;
  void* user_data;

  // States:
  const uint8_t* pattern_bytes;

  // Options:
  bool is_extended_mode;
  bool is_ignore_case;
  bool dot_allows_newline;
  bool char_class_is_strict;
  bool char_type_is_ascii_only;
  bool posix_char_class_is_ascii_only;
  nk_fold_flag_t fold_flags;

  // Limits:
  uint32_t range_quantifier_max_repetition_limit;
  uint32_t bare_back_ref_max_num_limit;
  uint32_t max_capture_num_limit;
  uint32_t back_ref_max_num_limit;
  uint32_t max_capture_depth_limit;
  uint32_t max_parse_depth_limit;

  // Internal states:
  bool in_unicode_escape_brace;
  uint32_t num_capture_groups;
  uint32_t parse_depth;

  // Statistics:
  bool has_named_captures;
  nk_capture_entry_t* capture_entries;
  size_t capture_entries_len;
  size_t capture_entries_cap;
  nk_capture_names_map_t* capture_names_map;

  // Error reporting:
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

  // Limits:
  uint32_t range_quantifier_max_repetition_limit;
  uint32_t bare_back_ref_max_num_limit;
  uint32_t max_capture_num_limit;
  uint32_t back_ref_max_num_limit;
  uint32_t max_capture_depth_limit;
  uint32_t max_parse_depth_limit;

  nk_warning_func_t warning_func;
  void* user_data;
} nk_parser_options_t;

static inline nk_parser_options_t nk_parser_options_default(void) {
  return (nk_parser_options_t){
    .is_extended_mode = false,
    .is_ignore_case = false,
    .dot_allows_newline = false,
    .char_class_is_strict = false,
    .char_type_is_ascii_only = true,
    .posix_char_class_is_ascii_only = false,
    .fold_flags = NK_FOLD_DEFAULT,
    .range_quantifier_max_repetition_limit = NK_DEFAULT_RANGE_QUANTIFIER_MAX_REPETITION,
    .bare_back_ref_max_num_limit = NK_DEFAULT_BARE_BACK_REF_MAX_NUM,
    .max_capture_num_limit = NK_DEFAULT_MAX_CAPTURE_NUM,
    .back_ref_max_num_limit = NK_DEFAULT_BACK_REF_MAX_NUM,
    .max_capture_depth_limit = NK_DEFAULT_MAX_CAPTURE_DEPTH,
    .max_parse_depth_limit = NK_DEFAULT_MAX_PARSE_DEPTH,
    .warning_func = (nk_warning_func_t)0,
    .user_data = (void*)0,
  };
}

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
 * Post-processes the parsed AST.
 */
NARAKU_EXPORTED_FUNCTION
nk_error_t nk_parser_postprocess(nk_parser_t* parser, nk_node_t* root_node);

/**
 * Copies resolved capture numbers for a resolved reference node
 * (`back_ref`, `conditional`) into `out_capture_nums`.
 */
NARAKU_EXPORTED_FUNCTION
nk_error_t
nk_node_get_resolved_capture_nums(const nk_parser_t* parser, const nk_node_t* node, uint32_t* out_capture_nums);

/**
 * Releases the memory allocated for the regex parser.
 */
NARAKU_EXPORTED_FUNCTION
void nk_parser_free(nk_parser_t* parser);

#ifdef __cplusplus
}
#endif

#endif  // NARAKU_SYNTAX_H
