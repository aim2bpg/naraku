/**
 * @file naraku_regex.h
 */

#ifndef NARAKU_REGEX_H
#define NARAKU_REGEX_H

#include <naraku_common.h>
#include <naraku_encoding.h>
#include <naraku_error.h>
#include <naraku_syntax.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
//
// Regex VM program:
//
// ============================================================================

/**
 * Enumeration of regex VM operations.
 *
 * The VM is a Pike-style thread-based NFA simulator: it advances a set of
 * prioritized threads over the subject one character at a time, so matching
 * runs in `O(subject length * program size)` time without backtracking.
 */
typedef enum {
  NK_VM_OP_CODE = 0,       // match a single code point
  NK_VM_OP_CHAR_CLASS,     // match a code point against a character class
  NK_VM_OP_DOT,            // match any code point (`.`)
  NK_VM_OP_ASSERTION,      // zero-width assertion (e.g., `^`, `$`, `\b`)
  NK_VM_OP_CAP_BEGIN,      // record the beginning of a capture group
  NK_VM_OP_CAP_END,        // record the end of a capture group
  NK_VM_OP_KEEP,           // `\K` (reset the reported match start)
  NK_VM_OP_JUMP,           // unconditional epsilon transition
  NK_VM_OP_SPLIT,          // prioritized epsilon branch (`next` first)
  NK_VM_OP_CHECK_VISITED,  // dedup point for merged epsilon paths
  NK_VM_OP_MARK_EPSILON,   // mark entering an empty-matchable loop body
  NK_VM_OP_CHECK_EPSILON,  // leave the loop if the body matched empty
  NK_VM_OP_MATCH,          // accept
} nk_vm_op_t;

/**
 * A special value indicating that a state transition target is not set.
 */
#define NK_VM_STATE_NONE UINT32_MAX

/**
 * Structure representing a single regex VM state (instruction).
 */
typedef struct {
  nk_vm_op_t op;
  uint32_t code;                       // valid for `NK_VM_OP_CODE`
  uint32_t char_class_index;           // valid for `NK_VM_OP_CHAR_CLASS`
  bool allows_newline;                 // valid for `NK_VM_OP_DOT`
  nk_assertion_type_t assertion_type;  // valid for `NK_VM_OP_ASSERTION`
  uint32_t cap_num;                    // valid for `NK_VM_OP_CAP_BEGIN` and `NK_VM_OP_CAP_END`
  // Dedup id for `NK_VM_OP_CODE`, `NK_VM_OP_CHAR_CLASS`, `NK_VM_OP_DOT`,
  // `NK_VM_OP_MATCH`, and `NK_VM_OP_CHECK_VISITED`; epsilon-loop id for
  // `NK_VM_OP_MARK_EPSILON` and `NK_VM_OP_CHECK_EPSILON`.
  uint32_t check_id;
  uint32_t next;
  uint32_t split_next;  // valid for `NK_VM_OP_SPLIT` and `NK_VM_OP_CHECK_EPSILON`
} nk_vm_state_t;

/**
 * Structure representing a compiled character class.
 *
 * `ranges` is an inversion list: `2 * ranges_len` code points forming sorted,
 * non-overlapping, inclusive `[begin, end]` pairs. `ascii_bits` is a bitmap
 * fast path for code points below `0x80`.
 *
 * `ascii_lookup[b]` is non-zero iff byte `b` is a member of the class.  Bytes
 * 0x80–0xFF are always 0 (they are never in an ASCII char class).  This flat
 * table makes the consecutive-run inner loop a single load instead of a
 * two-step bit extraction, enabling auto-vectorisation by the compiler.
 */
typedef struct {
  size_t ranges_len;
  uint32_t* ranges;
  uint64_t ascii_bits[2];
  uint8_t ascii_lookup[256];
} nk_vm_char_class_t;

/**
 * One slot in the lazy DFA transition cache.
 *
 * Slots are open-addressed with linear probing.  An empty slot has
 * `occupied == 0` (guaranteed by `calloc`).  The lookup key is the pair
 * (state_key, char_byte); `next_key` is the cached result.
 */
typedef struct {
  uint64_t state_key;  // NFA active-state bitmask (key)
  uint64_t next_key;   // resulting NFA bitmask after consuming char_byte
  uint8_t  char_byte;  // ASCII byte 0–127
  uint8_t  occupied;   // 0 = empty, 1 = in use
  uint8_t  pad[6];     // explicit padding to keep the struct 24 bytes
} nk_lazy_dfa_slot_t;

/** Number of slots in the lazy DFA cache (must be a power of two). */
#define NK_LAZY_DFA_SLOTS 1024u

/**
 * Lazy DFA transition cache for bitset-compatible programs.
 *
 * Caches (NFA-state-bitmask, ASCII-byte) → next-NFA-state-bitmask entries
 * computed during `search_impl_bitset`.  Amortises the inner bit-scan loop
 * for repeated (state-set, character) pairs across calls on the same program.
 * Non-NULL only when `goto_mask != NULL`.
 *
 * `fill` counts occupied slots.  When `fill` reaches 75% of `NK_LAZY_DFA_SLOTS`
 * the cache is wiped and rebuilt from scratch to prevent probe-chain
 * degradation without unbounded memory growth.
 */
typedef struct {
  nk_lazy_dfa_slot_t slots[NK_LAZY_DFA_SLOTS];
  uint32_t fill;
} nk_lazy_dfa_t;

/**
 * Structure representing a compiled regex VM program.
 */
typedef struct nk_program {
  const nk_encoding_t* enc;
  nk_vm_state_t* states;
  size_t states_len;
  uint32_t initial_state;
  uint32_t num_capture_groups;
  uint32_t num_check_ids;
  uint32_t num_epsilon_check_ids;
  nk_vm_char_class_t* char_classes;
  size_t char_classes_len;
  bool is_anchored;       // true when the pattern is anchored to \A (never matches after position 0)
  bool is_pure_literal;  // true when the entire pattern is one case-sensitive ASCII literal
  // Leading case-sensitive literal byte sequence extracted from the pattern.
  // Non-NULL only when all bytes are ASCII (<0x80), which is safe for all
  // supported encodings (ASCII bytes are never continuation bytes in UTF-8,
  // Shift-JIS, etc.). Used by the VM for a fast memmem pre-scan.
  uint8_t* literal_prefix_bytes;
  size_t literal_prefix_len;
  // Single ASCII byte that must appear in any match (prefilter for early no-match
  // rejection). Only set when `has_required_byte` is true and `literal_prefix_len`
  // is 0 (when a prefix is available it already implies this byte).
  bool    has_required_byte;
  uint8_t required_byte;
  // Multi-literal alternation prefilter.  When the top-level pattern is a pure
  // alternation of case-sensitive ASCII literals (e.g., `foo|bar|baz`), all
  // literals are stored here so the VM can scan for the earliest occurrence
  // with multiple memmem calls before starting the NFA.  NULL when not applicable.
  uint8_t** alt_literal_bytes;
  size_t*   alt_literal_lens;
  size_t    alt_literal_count;
  // Thompson NFA bitset for fast boolean match? on small assertion-free programs.
  // Bit i in goto_mask[j] means "after consuming a char from consuming state j,
  // state i may be active". Bit 63 (NK_BITSET_MATCH_BIT) signals MATCH reachable.
  // NULL when states_len > 63 or any ASSERTION / KEEP state exists.
  uint64_t* goto_mask;    // [states_len] (only indices of consuming states are used)
  uint64_t  initial_mask; // epsilon closure from initial_state (consuming bits + MATCH bit)
  // Lazy DFA cache: populated by search_impl_bitset.  NULL when goto_mask is NULL.
  nk_lazy_dfa_t* lazy_dfa;
} nk_program_t;

/** Bit 63 of a bitset mask signals that a MATCH state is reachable. */
#define NK_BITSET_MATCH_BIT ((uint64_t)1u << 63)

/**
 * The maximum number of VM states in a compiled program.
 *
 * Compilation fails with `NK_ERR_PATTERN_TOO_COMPLEX` if the program would
 * exceed this limit (e.g., for huge counted repetitions).
 */
#define NK_MAX_VM_STATES (1u << 18)

/**
 * The maximum number of epsilon-loop check ids in a compiled program.
 *
 * Compilation fails with `NK_ERR_PATTERN_TOO_COMPLEX` if the pattern needs
 * more (i.e., it nests more than this many empty-matchable quantifiers).
 */
#define NK_MAX_VM_EPSILON_CHECK_IDS 64u

// ============================================================================
//
// Match region:
//
// ============================================================================

/**
 * A special value indicating that a capture position is not set.
 */
#define NK_REGION_POS_NONE SIZE_MAX

/**
 * Structure representing capture results of a match.
 *
 * `caps` holds `2 * num_caps` byte offsets into the subject:
 * `[begin_0, end_0, begin_1, end_1, ...]` where index 0 is the whole match.
 * Unset positions are `NK_REGION_POS_NONE`.
 */
typedef struct {
  size_t num_caps;  // `num_capture_groups + 1`
  size_t* caps;
} nk_region_t;

/**
 * Initializes a region for a regex with `num_capture_groups` capture groups.
 */
NARAKU_EXPORTED_FUNCTION
nk_error_t nk_region_init(nk_region_t* region, uint32_t num_capture_groups);

/**
 * Releases the memory allocated for a region.
 */
NARAKU_EXPORTED_FUNCTION
void nk_region_free(nk_region_t* region);

// ==========================================================================
//
// src/regex_compile.c
//
// ==========================================================================

/**
 * Compiles a parsed (and post-processed) regex AST into a VM program.
 *
 * `root_node` may be `NULL` for an empty pattern. On success, writes the
 * compiled program to `*out_program`; the program does not reference the AST
 * or the pattern buffer afterwards, so both may be freed independently.
 *
 * On failure, writes the span of the offending AST node to
 * `*out_error_offset` / `*out_error_length` (if non-NULL) and returns a
 * negative error code (e.g., `NK_ERR_UNSUPPORTED_*` for features not yet
 * implemented by the VM compiler).
 */
NARAKU_EXPORTED_FUNCTION
nk_error_t nk_program_compile(
  const nk_encoding_t* enc,
  const nk_node_t* root_node,
  uint32_t num_capture_groups,
  nk_program_t** out_program,
  size_t* out_error_offset,  // nullable
  size_t* out_error_length   // nullable
);

/**
 * Releases the memory allocated for a compiled program.
 */
NARAKU_EXPORTED_FUNCTION
void nk_program_free(nk_program_t* program);

// ==========================================================================
//
// src/regex_vm.c
//
// ==========================================================================

/**
 * Searches the subject for the leftmost match of the program.
 *
 * `start_offset` is a byte offset into the subject and must point at a
 * character boundary. If `out_region` is non-NULL, it must have been
 * initialized with `nk_region_init(region, program->num_capture_groups)`,
 * and capture positions (byte offsets) are written to it on success.
 *
 * Returns `NK_SUCCESS` on match, `NK_NO_MATCH` if there is no match, or a
 * negative error code (e.g., `NK_ERR_INVALID_BYTE_SEQUENCE` if the subject
 * is not valid in the program's encoding).
 */
NARAKU_EXPORTED_FUNCTION
nk_error_t nk_program_search(
  const nk_program_t* program,
  const uint8_t* subject_bytes,
  const uint8_t* subject_bytes_end,
  size_t start_offset,
  nk_region_t* out_region  // nullable
);

/**
 * Tests whether the subject matches the program without recording captures.
 *
 * Equivalent to `nk_program_search` with a NULL region, but avoids all
 * capture-buffer allocation. Use this for `match?`-style boolean queries.
 *
 * Returns `NK_SUCCESS` on match, `NK_NO_MATCH` if there is no match, or a
 * negative error code.
 */
NARAKU_EXPORTED_FUNCTION
nk_error_t nk_program_search_boolean(
  const nk_program_t* program,
  const uint8_t* subject_bytes,
  const uint8_t* subject_bytes_end,
  size_t start_offset
);

#ifdef __cplusplus
}
#endif

#endif  // NARAKU_REGEX_H
