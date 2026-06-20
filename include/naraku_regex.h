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
  NK_VM_OP_BACK_REF,       // match a back reference to a capture group (`\1`, `\k<name>`)
  NK_VM_OP_LOOKAROUND,     // zero-width lookahead/lookbehind sub-pattern test
  NK_VM_OP_POSSESSIVE,     // possessive quantifier — commit to greedy max, no backtrack
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
  // Case folding for `NK_VM_OP_CODE`.  `is_ignore_case` enables folding;
  // `fold_flags` controls which fold table to use:
  //   NK_FOLD_DEFAULT    — simple 1-to-1 Unicode fold (e.g. Ä ↔ ä)
  //   NK_FOLD_ASCII_ONLY — A-Z / a-z only, no encoding API call
  nk_fold_flag_t fold_flags;
  bool is_ignore_case;
  // For `NK_VM_OP_LOOKAROUND`: sub-program index (into `nk_program_t::sub_programs`),
  // whether the assertion is positive (true = (?=)/(?<=)) or negative (false = (?!)/(?<!)),
  // and whether it looks ahead (true) or behind (false).
  uint32_t lookaround_prog_idx;
  bool lookaround_is_positive;
  bool lookaround_is_ahead;
  // For `NK_VM_OP_POSSESSIVE`: index of the greedy sub-program to run.
  uint32_t possessive_prog_idx;
} nk_vm_state_t;

/** Max number of contiguous ASCII byte ranges the SIMD run-scan fast path supports. */
#define NK_CC_SIMD_MAX_RANGES 4u

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
 *
 * `simd_range_count`/`simd_ranges` drive the SIMD-accelerated ASCII run scan
 * (see `ascii_class_run_length` in regex_vm.c). Set at compile time when the
 * ASCII members of this class decompose into at most `NK_CC_SIMD_MAX_RANGES`
 * contiguous byte ranges (true for `\d`, `\w`, `[a-zA-Z0-9]`, and similar).
 * `simd_range_count == 0` means the class doesn't qualify; callers always
 * fall back to the scalar `ascii_lookup` scan.
 */
typedef struct {
  size_t ranges_len;
  uint32_t* ranges;
  uint64_t ascii_bits[2];
  uint8_t ascii_lookup[256];
  uint8_t simd_range_count;
  uint8_t simd_ranges[NK_CC_SIMD_MAX_RANGES][2];  // [i][0] = lo, [i][1] = hi (inclusive, ASCII only)
} nk_vm_char_class_t;

/**
 * 128-bit bitset used by the Thompson NFA "bitset" fast path (`nk_program_t::goto_mask`
 * below). Bit `i` (0-indexed) lives in `lo` for `i < 64`, in `hi` for `i >= 64`.
 *
 * Implemented as a struct of two `uint64_t` rather than the GCC/Clang `__uint128_t`
 * extension: `__uint128_t` is rejected under `-std=c99 -Wpedantic`, which this
 * project builds with (see CLAUDE.md "Compiler flags").
 */
typedef struct {
  uint64_t lo;
  uint64_t hi;
} nk_bitset128_t;

#define NK_BITSET128_ZERO ((nk_bitset128_t){0, 0})

/** Bit 127 of a 128-bit bitset signals that a MATCH state is reachable. */
#define NK_BITSET128_MATCH_BIT ((nk_bitset128_t){0, (uint64_t)1u << 63})

static inline nk_bitset128_t nk_bitset128_or(nk_bitset128_t a, nk_bitset128_t b) {
  return (nk_bitset128_t){a.lo | b.lo, a.hi | b.hi};
}

static inline nk_bitset128_t nk_bitset128_and(nk_bitset128_t a, nk_bitset128_t b) {
  return (nk_bitset128_t){a.lo & b.lo, a.hi & b.hi};
}

/** Returns `a & ~b`. */
static inline nk_bitset128_t nk_bitset128_andnot(nk_bitset128_t a, nk_bitset128_t b) {
  return (nk_bitset128_t){a.lo & ~b.lo, a.hi & ~b.hi};
}

static inline bool nk_bitset128_is_zero(nk_bitset128_t a) {
  return a.lo == 0 && a.hi == 0;
}

static inline bool nk_bitset128_eq(nk_bitset128_t a, nk_bitset128_t b) {
  return a.lo == b.lo && a.hi == b.hi;
}

static inline bool nk_bitset128_has_match_bit(nk_bitset128_t a) {
  return (a.hi & ((uint64_t)1u << 63)) != 0;
}

/** Sets bit `idx` (0-127) and returns the result. */
static inline nk_bitset128_t nk_bitset128_set_bit(nk_bitset128_t a, uint32_t idx) {
  if (idx < 64u) {
    a.lo |= (uint64_t)1u << idx;
  } else {
    a.hi |= (uint64_t)1u << (idx - 64u);
  }
  return a;
}

/** Tests bit `idx` (0-127). */
static inline bool nk_bitset128_test_bit(nk_bitset128_t a, uint32_t idx) {
  if (idx < 64u) {
    return (a.lo & ((uint64_t)1u << idx)) != 0;
  }
  return (a.hi & ((uint64_t)1u << (idx - 64u))) != 0;
}

/**
 * One slot in the lazy DFA transition cache (wide variant — used when a
 * program needs the full 64–127 state range; see `nk_lazy_dfa_slot_narrow_t`
 * below for the ≤63-state variant).
 *
 * Slots are open-addressed with linear probing.  An empty slot has
 * `occupied == 0` (guaranteed by `calloc`).  The lookup key is the pair
 * (state_key, char_byte); `next_key` is the cached result.
 */
typedef struct {
  nk_bitset128_t state_key;  // NFA active-state bitmask (key)
  nk_bitset128_t next_key;   // resulting NFA bitmask after consuming char_byte
  uint8_t char_byte;         // ASCII byte 0–127
  uint8_t occupied;          // 0 = empty, 1 = in use
  uint8_t pad[6];            // explicit padding for alignment
} nk_lazy_dfa_slot_t;

/**
 * One slot in the lazy DFA transition cache (narrow variant — used when a
 * program's state count fits in 63 states, i.e. `nk_bitset128_t::hi` is
 * always 0). Same layout Naraku used before the bitset was widened to 128
 * bits: 24 bytes instead of 40, so `NK_LAZY_DFA_SLOTS` of them fit in 24KB
 * rather than 40KB — small enough to stay resident in a typical 32KB L1d
 * cache. Storing the full 128-bit key here would be correct but wasteful
 * (the high word is always 0), and the larger 1024-slot array no longer
 * fits in L1d, which measurably regressed already-good small patterns
 * (`ambiguous`, `bounded`, `repetition`) when the bitset was widened.
 */
typedef struct {
  uint64_t state_key;  // NFA active-state bitmask (key); only the low 63 bits are ever used
  uint64_t next_key;   // resulting NFA bitmask after consuming char_byte
  uint8_t char_byte;   // ASCII byte 0–127
  uint8_t occupied;    // 0 = empty, 1 = in use
  uint8_t pad[6];      // explicit padding to keep the struct 24 bytes
} nk_lazy_dfa_slot_narrow_t;

/** Number of slots in the lazy DFA cache (must be a power of two). */
#define NK_LAZY_DFA_SLOTS 1024u

/**
 * Lazy DFA transition cache for bitset-compatible programs that need states
 * 64–127 (i.e. `nk_bitset128_t::hi` may be non-zero). See
 * `nk_lazy_dfa_narrow_t` for the ≤63-state variant, which is preferred
 * whenever it applies because of its smaller cache-line footprint.
 *
 * Caches (NFA-state-bitmask, ASCII-byte) → next-NFA-state-bitmask entries
 * computed during `search_impl_bitset`.  Amortises the inner bit-scan loop
 * for repeated (state-set, character) pairs across calls on the same program.
 * Non-NULL only when `goto_mask != NULL` and the program needs states beyond 63.
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
 * Lazy DFA transition cache for bitset-compatible programs whose state count
 * fits in 63 states. Preferred over `nk_lazy_dfa_t` whenever applicable —
 * see `nk_lazy_dfa_slot_narrow_t` for why. `fill`/75%-reset semantics match
 * `nk_lazy_dfa_t` exactly.
 */
typedef struct {
  nk_lazy_dfa_slot_narrow_t slots[NK_LAZY_DFA_SLOTS];
  uint32_t fill;
} nk_lazy_dfa_narrow_t;

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
  bool is_anchored;          // true when the pattern is anchored to \A (never matches after position 0)
  bool is_pure_literal;      // true when the entire pattern is one case-sensitive ASCII literal
  bool is_pure_alt_literal;  // true when the entire pattern is an alternation of ASCII literals with no capture groups
  // Non-ASCII pure literal/alternation bypass: like alt_literal_bytes but allows
  // non-ASCII bytes.  Set when the entire pattern is a literal or alternation of
  // literals (any encoding) with no capture groups and is not already covered by
  // is_pure_literal / is_pure_alt_literal.  Used only for the VM bypass.
  uint8_t** full_alt_bytes;
  size_t* full_alt_lens;
  size_t full_alt_count;
  // Leading case-sensitive literal byte sequence extracted from the pattern.
  // Non-NULL only when all bytes are ASCII (<0x80), which is safe for all
  // supported encodings (ASCII bytes are never continuation bytes in UTF-8,
  // Shift-JIS, etc.). Used by the VM for a fast memmem pre-scan.
  uint8_t* literal_prefix_bytes;
  size_t literal_prefix_len;
  // Single ASCII byte that must appear in any match (prefilter for early no-match
  // rejection). Only set when `has_required_byte` is true and `literal_prefix_len`
  // is 0 (when a prefix is available it already implies this byte).
  bool has_required_byte;
  uint8_t required_byte;
  // Multi-literal alternation prefilter.  When the top-level pattern is a pure
  // alternation of case-sensitive ASCII literals (e.g., `foo|bar|baz`), all
  // literals are stored here so the VM can scan for the earliest occurrence
  // with multiple memmem calls before starting the NFA.  NULL when not applicable.
  uint8_t** alt_literal_bytes;
  size_t* alt_literal_lens;
  size_t alt_literal_count;
  // Thompson NFA bitset for fast boolean match? on small assertion-free programs.
  // Bit i in goto_mask[j] means "after consuming a char from consuming state j,
  // state i may be active". Bit 127 (NK_BITSET128_MATCH_BIT) signals MATCH reachable.
  // NULL when states_len > 127 or any ASSERTION / KEEP state exists.
  nk_bitset128_t* goto_mask;    // [states_len] (only indices of consuming states are used)
  nk_bitset128_t initial_mask;  // epsilon closure from initial_state (consuming bits + MATCH bit)
  // Lazy DFA cache: populated by search_impl_bitset. Exactly one of these is
  // non-NULL when goto_mask != NULL (narrow for states_len <= 63, wide for
  // 64-127); both are NULL when goto_mask is NULL.
  nk_lazy_dfa_t* lazy_dfa;
  nk_lazy_dfa_narrow_t* lazy_dfa_narrow;
  // First-byte table for jump optimization in search_impl_bitset.
  // When valid, first_byte_table[b] != 0 means ASCII byte b can be the first byte
  // consumed from the initial NFA state.  Positions where first_byte_table[byte] == 0
  // can be skipped when the NFA is in the reset (initial_mask) state.
  // NULL when goto_mask is NULL or first-byte scan is not applicable (loops,
  // non-ASCII initial states, DOT initial states).
  bool first_byte_table_valid;
  uint8_t first_byte_table[128];  // ASCII-only (bytes 0x00–0x7F)
  // Pure char-class loop bypass: true when the pattern is [X]+ (or \w+, \d+ etc.)
  // with min >= 1, no capture groups, and at least one ASCII member in the class.
  // Enables a direct ascii_lookup scan that skips the NFA entirely.
  bool is_pure_char_class_plus;
  uint32_t pure_cc_index;  // index into char_classes[]
  // True when the program contains NK_VM_OP_BACK_REF states.
  // Disables the no_caps optimization (which would make caps->data invalid).
  bool has_back_refs;
  // Sub-programs compiled from lookahead/lookbehind bodies.
  // Owned and freed by nk_program_free(); indexed by NK_VM_OP_LOOKAROUND states.
  struct nk_program** sub_programs;
  size_t sub_programs_len;
} nk_program_t;

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
  const nk_capture_entry_t* capture_entries,  // nullable; only needed when pattern has back-refs
  size_t capture_entries_len,
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
