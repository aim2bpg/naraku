/**
 * @file regex_vm.c
 */

#include <naraku_regex.h>

#include <stdlib.h>
#include <string.h>

// A sentinel code point meaning "no character here" (before the subject,
// or at/after its end).
#define VM_NO_CHAR UINT32_MAX

// Sentinel pointer used in no-capture mode. All caps_* functions treat this
// as a valid but permanently-shared reference that requires no heap allocation
// and ignores all writes.
#define NO_CAPS_PTR ((caps_t*)1)

// Finds the first occurrence of `needle[0..nlen-1]` in `haystack[0..hlen-1]`.
// Uses `memchr` for the fast initial scan (SIMD on most platforms), then
// `memcmp` for verification. Returns NULL if not found.
static const uint8_t* vm_memmem(const uint8_t* haystack, size_t hlen, const uint8_t* needle, size_t nlen) {
  if (nlen == 0) {
    return haystack;
  }
  if (hlen < nlen) {
    return NULL;
  }
  const uint8_t* end = haystack + (hlen - nlen);
  int first = (int)(unsigned int)needle[0];
  const uint8_t* p = haystack;
  while (p <= end) {
    p = (const uint8_t*)memchr(p, first, (size_t)(end - p) + 1);
    if (p == NULL) {
      return NULL;
    }
    if (nlen == 1 || memcmp(p, needle, nlen) == 0) {
      return p;
    }
    p++;
  }
  return NULL;
}

// The code point of `\n`.
#define VM_NEWLINE_CODE UINT32_C(0x0A)

// ============================================================================
//
// Copy-on-write capture buffers:
//
// ============================================================================

// A reference-counted capture buffer shared between VM threads. `data` holds
// `2 * num_caps` byte offsets: `[begin_0, end_0, begin_1, end_1, ...]`.
// Forking a thread just bumps the reference count; the buffer is copied only
// when a shared one is written to.
typedef struct {
  uint32_t refcount;
  size_t data[];
} caps_t;

static caps_t* caps_new(size_t num_caps) {
  caps_t* caps = (caps_t*)malloc(sizeof(caps_t) + 2 * num_caps * sizeof(size_t));
  if (caps == NULL) {
    return NULL;
  }
  caps->refcount = 1;
  for (size_t i = 0; i < 2 * num_caps; i++) {
    caps->data[i] = NK_REGION_POS_NONE;
  }
  return caps;
}

static caps_t* caps_ref(caps_t* caps) {
  if (caps == NO_CAPS_PTR) {
    return caps;
  }
  caps->refcount++;
  return caps;
}

static void caps_unref(caps_t* caps) {
  if (caps == NULL || caps == NO_CAPS_PTR) {
    return;
  }
  caps->refcount--;
  if (caps->refcount == 0) {
    free(caps);
  }
}

// ============================================================================
//
// VM threads and the epsilon-closure work stack:
//
// ============================================================================

// A suspended VM thread, waiting at a character-consuming state.
// Each thread owns one reference to its capture buffer.
typedef struct {
  uint32_t state_index;
  size_t keep_pos;  // the match start to report (moved by `\K`)
  caps_t* caps;
} thread_t;

typedef struct {
  size_t len;
  size_t cap;
  thread_t* items;
} thread_list_t;

// A work item of the iterative epsilon closure. When `is_mark` is set, the
// item marks `check_id` as visited after the subtree pushed above it has been
// fully processed (post-order, matching the prototype's recursion).
typedef struct {
  bool is_mark;
  uint32_t state_index;  // holds the check id when `is_mark` is set
  size_t keep_pos;
  uint64_t epsilon_bits;
  caps_t* caps;  // owned reference; NULL for mark items
} work_item_t;

typedef struct {
  const nk_program_t* program;
  size_t num_caps;  // num_capture_groups + 1
  bool no_caps;     // when true, use NO_CAPS_PTR everywhere (skip all heap allocation)
  size_t start_offset;

  // Visited set, keyed by state check ids. An id is visited in the current
  // step when `visited[id] == token`; bumping `token` clears the whole set.
  uint32_t* visited;
  uint32_t token;

  // Reusable explicit stack for the epsilon closure.
  work_item_t* stack;
  size_t stack_len;
  size_t stack_cap;

  // The character window around the current closure position, used by
  // assertions: the characters just before and at `closure_pos`, and the one
  // after that (`VM_NO_CHAR` when absent).
  size_t closure_pos;
  uint32_t prev_code;
  uint32_t curr_code;
  uint32_t next_code;

  // The best match recorded so far. Every newly recorded match comes from a
  // higher-priority thread than the previous one (lower-priority work is
  // discarded as soon as a match is recorded), so recording always replaces.
  bool has_match;
  uint64_t match_version;
  size_t match_keep_pos;
  caps_t* match_caps;  // owned reference
} vm_t;

static void thread_list_free(thread_list_t* threads) {
  for (size_t i = 0; i < threads->len; i++) {
    caps_unref(threads->items[i].caps);
  }
  free(threads->items);
  threads->items = NULL;
  threads->len = 0;
  threads->cap = 0;
}

static nk_error_t thread_list_push(thread_list_t* threads, uint32_t state_index, size_t keep_pos, caps_t* caps) {
  if (threads->len == threads->cap) {
    size_t new_cap = threads->cap == 0 ? 16 : threads->cap * 2;
    thread_t* new_items = (thread_t*)realloc(threads->items, new_cap * sizeof(thread_t));
    if (new_items == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    threads->items = new_items;
    threads->cap = new_cap;
  }
  threads->items[threads->len].state_index = state_index;
  threads->items[threads->len].keep_pos = keep_pos;
  threads->items[threads->len].caps = caps;
  threads->len++;
  return NK_SUCCESS;
}

// Drops every remaining work item, releasing the capture references they own.
static void drain_stack(vm_t* vm) {
  while (vm->stack_len > 0) {
    work_item_t* item = &vm->stack[--vm->stack_len];
    if (!item->is_mark) {
      caps_unref(item->caps);
    }
  }
}

static nk_error_t stack_reserve(vm_t* vm) {
  if (vm->stack_len < vm->stack_cap) {
    return NK_SUCCESS;
  }
  size_t new_cap = vm->stack_cap == 0 ? 32 : vm->stack_cap * 2;
  work_item_t* new_stack = (work_item_t*)realloc(vm->stack, new_cap * sizeof(work_item_t));
  if (new_stack == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  vm->stack = new_stack;
  vm->stack_cap = new_cap;
  return NK_SUCCESS;
}

// Pushes a state to process. Takes ownership of `caps`: it is released if the
// push fails.
static nk_error_t push_state(vm_t* vm, uint32_t state_index, size_t keep_pos, uint64_t epsilon_bits, caps_t* caps) {
  nk_error_t err = stack_reserve(vm);
  if (err != NK_SUCCESS) {
    caps_unref(caps);
    return err;
  }
  work_item_t* item = &vm->stack[vm->stack_len++];
  item->is_mark = false;
  item->state_index = state_index;
  item->keep_pos = keep_pos;
  item->epsilon_bits = epsilon_bits;
  item->caps = caps;
  return NK_SUCCESS;
}

static nk_error_t push_mark(vm_t* vm, uint32_t check_id) {
  nk_error_t err = stack_reserve(vm);
  if (err != NK_SUCCESS) {
    return err;
  }
  work_item_t* item = &vm->stack[vm->stack_len++];
  item->is_mark = true;
  item->state_index = check_id;
  item->keep_pos = 0;
  item->epsilon_bits = 0;
  item->caps = NULL;
  return NK_SUCCESS;
}

// Writes one capture slot, copying the buffer first if it is shared.
static nk_error_t caps_write(vm_t* vm, caps_t** caps_ptr, size_t index, size_t value) {
  caps_t* caps = *caps_ptr;
  if (caps == NO_CAPS_PTR) {
    return NK_SUCCESS;
  }
  if (caps->refcount > 1) {
    caps_t* copy = caps_new(vm->num_caps);
    if (copy == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    memcpy(copy->data, caps->data, 2 * vm->num_caps * sizeof(size_t));
    caps->refcount--;
    *caps_ptr = copy;
    caps = copy;
  }
  caps->data[index] = value;
  return NK_SUCCESS;
}

// ============================================================================
//
// Character classes and assertions:
//
// ============================================================================

static bool char_class_contains(const nk_vm_char_class_t* char_class, uint32_t code) {
  if (code < 0x80) {
    return (char_class->ascii_bits[code >> 6] & ((uint64_t)1 << (code & 0x3F))) != 0;
  }

  size_t lo = 0;
  size_t hi = char_class->ranges_len;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (char_class->ranges[2 * mid + 1] < code) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo < char_class->ranges_len && char_class->ranges[2 * lo] <= code;
}

static bool code_is_word(const nk_encoding_t* enc, uint32_t code) {
  return code != VM_NO_CHAR && nk_enc_code_is_cprop(enc, code, NK_CPROP_WORD);
}

static bool code_is_ascii_word(const nk_encoding_t* enc, uint32_t code) {
  return code != VM_NO_CHAR && code < 0x80 && nk_enc_code_is_cprop(enc, code, NK_CPROP_WORD);
}

static bool eval_assertion(const vm_t* vm, nk_assertion_type_t type) {
  const nk_encoding_t* enc = vm->program->enc;

  switch (type) {
    case NK_ASSERTION_TYPE_BEGIN_OF_LINE:
      return vm->closure_pos == 0 || vm->prev_code == VM_NEWLINE_CODE;
    case NK_ASSERTION_TYPE_END_OF_LINE:
      return vm->curr_code == VM_NO_CHAR || vm->curr_code == VM_NEWLINE_CODE;
    case NK_ASSERTION_TYPE_BEGIN_OF_STRING:
      return vm->closure_pos == 0;
    case NK_ASSERTION_TYPE_END_OF_STRING_STRICT:
      return vm->curr_code == VM_NO_CHAR;
    case NK_ASSERTION_TYPE_END_OF_STRING_LOOSE:
      return vm->curr_code == VM_NO_CHAR || (vm->curr_code == VM_NEWLINE_CODE && vm->next_code == VM_NO_CHAR);
    case NK_ASSERTION_TYPE_BEGIN_OF_MATCHING:
      return vm->closure_pos == vm->start_offset;
    case NK_ASSERTION_TYPE_WORD_BOUNDARY:
      return code_is_word(enc, vm->prev_code) != code_is_word(enc, vm->curr_code);
    case NK_ASSERTION_TYPE_NON_WORD_BOUNDARY:
      return code_is_word(enc, vm->prev_code) == code_is_word(enc, vm->curr_code);
    case NK_ASSERTION_TYPE_ASCII_WORD_BOUNDARY:
      return code_is_ascii_word(enc, vm->prev_code) != code_is_ascii_word(enc, vm->curr_code);
    case NK_ASSERTION_TYPE_NON_ASCII_WORD_BOUNDARY:
      return code_is_ascii_word(enc, vm->prev_code) == code_is_ascii_word(enc, vm->curr_code);
    // Rejected by the compiler; never reached at run time.
    case NK_ASSERTION_TYPE_POSITIVE_LOOKAHEAD:
    case NK_ASSERTION_TYPE_NEGATIVE_LOOKAHEAD:
    case NK_ASSERTION_TYPE_POSITIVE_LOOKBEHIND:
    case NK_ASSERTION_TYPE_NEGATIVE_LOOKBEHIND:
      return false;
  }

  return false;
}

// Whether the consuming state at the head of a thread matches `code`.
// `code` is never `VM_NO_CHAR` here.
static bool state_matches_code(const nk_program_t* program, const nk_vm_state_t* state, uint32_t code) {
  switch (state->op) {
    case NK_VM_OP_CODE:
      return code == state->code;
    case NK_VM_OP_CHAR_CLASS:
      return char_class_contains(&program->char_classes[state->char_class_index], code);
    case NK_VM_OP_DOT:
      return state->allows_newline || code != VM_NEWLINE_CODE;
    default:
      return false;
  }
}

// ============================================================================
//
// Epsilon closure:
//
// ============================================================================

// Expands all epsilon transitions reachable from `start_state` at the current
// closure position, appending the reached consuming states to `out_threads`
// in priority order. Takes ownership of one reference to `caps`.
//
// When a `MATCH` state is reached, the match is recorded on `vm` and the rest
// of the stack (all lower-priority work) is discarded.
static nk_error_t closure(vm_t* vm, uint32_t start_state, size_t keep_pos, caps_t* caps, thread_list_t* out_threads) {
  const nk_vm_state_t* states = vm->program->states;

  nk_error_t err = push_state(vm, start_state, keep_pos, 0, caps);
  if (err != NK_SUCCESS) {
    return err;
  }

  while (vm->stack_len > 0) {
    work_item_t item = vm->stack[--vm->stack_len];
    if (item.is_mark) {
      vm->visited[item.state_index] = vm->token;
      continue;
    }

    const nk_vm_state_t* state = &states[item.state_index];
    switch (state->op) {
      case NK_VM_OP_CODE:
      case NK_VM_OP_CHAR_CLASS:
      case NK_VM_OP_DOT:
        if (vm->visited[state->check_id] == vm->token) {
          caps_unref(item.caps);
          break;
        }
        vm->visited[state->check_id] = vm->token;
        err = thread_list_push(out_threads, item.state_index, item.keep_pos, item.caps);
        if (err != NK_SUCCESS) {
          caps_unref(item.caps);
          goto fail;
        }
        break;

      case NK_VM_OP_MATCH:
        if (vm->visited[state->check_id] == vm->token) {
          caps_unref(item.caps);
          break;
        }
        vm->visited[state->check_id] = vm->token;
        caps_unref(vm->match_caps);
        vm->match_caps = item.caps;
        vm->match_keep_pos = item.keep_pos;
        vm->has_match = true;
        vm->match_version++;
        // Everything left on the stack has lower priority than this match.
        drain_stack(vm);
        return NK_SUCCESS;

      case NK_VM_OP_JUMP:
        err = push_state(vm, state->next, item.keep_pos, item.epsilon_bits, item.caps);
        if (err != NK_SUCCESS) {
          goto fail;
        }
        break;

      case NK_VM_OP_ASSERTION:
        if (eval_assertion(vm, state->assertion_type)) {
          err = push_state(vm, state->next, item.keep_pos, item.epsilon_bits, item.caps);
          if (err != NK_SUCCESS) {
            goto fail;
          }
        } else {
          caps_unref(item.caps);
        }
        break;

      case NK_VM_OP_CAP_BEGIN:
        err = caps_write(vm, &item.caps, 2 * (size_t)state->cap_num, vm->closure_pos);
        if (err == NK_SUCCESS) {
          err = caps_write(vm, &item.caps, 2 * (size_t)state->cap_num + 1, NK_REGION_POS_NONE);
        }
        if (err != NK_SUCCESS) {
          caps_unref(item.caps);
          goto fail;
        }
        err = push_state(vm, state->next, item.keep_pos, item.epsilon_bits, item.caps);
        if (err != NK_SUCCESS) {
          goto fail;
        }
        break;

      case NK_VM_OP_CAP_END:
        err = caps_write(vm, &item.caps, 2 * (size_t)state->cap_num + 1, vm->closure_pos);
        if (err != NK_SUCCESS) {
          caps_unref(item.caps);
          goto fail;
        }
        err = push_state(vm, state->next, item.keep_pos, item.epsilon_bits, item.caps);
        if (err != NK_SUCCESS) {
          goto fail;
        }
        break;

      case NK_VM_OP_KEEP:
        err = push_state(vm, state->next, vm->closure_pos, item.epsilon_bits, item.caps);
        if (err != NK_SUCCESS) {
          goto fail;
        }
        break;

      case NK_VM_OP_SPLIT:
      {
        // `next` has priority: push `split_next` first so that `next` is
        // popped (and thus fully explored) first.
        caps_t* forked = caps_ref(item.caps);
        err = push_state(vm, state->split_next, item.keep_pos, item.epsilon_bits, item.caps);
        if (err != NK_SUCCESS) {
          caps_unref(forked);
          goto fail;
        }
        err = push_state(vm, state->next, item.keep_pos, item.epsilon_bits, forked);
        if (err != NK_SUCCESS) {
          goto fail;
        }
        break;
      }

      case NK_VM_OP_CHECK_VISITED:
        if (vm->visited[state->check_id] == vm->token) {
          caps_unref(item.caps);
          break;
        }
        // Mark after the whole subtree below `next` has been processed
        // (post-order), as the prototype does.
        err = push_mark(vm, state->check_id);
        if (err != NK_SUCCESS) {
          caps_unref(item.caps);
          goto fail;
        }
        err = push_state(vm, state->next, item.keep_pos, item.epsilon_bits, item.caps);
        if (err != NK_SUCCESS) {
          goto fail;
        }
        break;

      case NK_VM_OP_MARK_EPSILON:
        err =
          push_state(vm, state->next, item.keep_pos, item.epsilon_bits | ((uint64_t)1 << state->check_id), item.caps);
        if (err != NK_SUCCESS) {
          goto fail;
        }
        break;

      case NK_VM_OP_CHECK_EPSILON:
      {
        // The bit is still set if the loop body consumed nothing since the
        // matching `MARK_EPSILON`: leave the loop instead of spinning.
        bool body_was_empty = (item.epsilon_bits & ((uint64_t)1 << state->check_id)) != 0;
        err =
          push_state(vm, body_was_empty ? state->split_next : state->next, item.keep_pos, item.epsilon_bits, item.caps);
        if (err != NK_SUCCESS) {
          goto fail;
        }
        break;
      }
    }
  }

  return NK_SUCCESS;

fail:
  drain_stack(vm);
  return err;
}

// ============================================================================
//
// Main search loop:
//
// ============================================================================

// Decodes the character at `bytes` (`VM_NO_CHAR` at the end of the subject).
static nk_error_t decode_char(
  const nk_encoding_t* enc,
  const uint8_t* bytes,
  const uint8_t* bytes_end,
  uint32_t* out_code,
  size_t* out_width
) {
  if (bytes >= bytes_end) {
    *out_code = VM_NO_CHAR;
    *out_width = 0;
    return NK_SUCCESS;
  }

  int8_t width = nk_enc_scan_mbc_width(enc, bytes, bytes_end);
  if (width == 0) {
    return NK_ERR_INVALID_BYTE_SEQUENCE;
  }
  if (width < 0) {
    return NK_ERR_INCOMPLETE_BYTE_SEQUENCE;
  }

  *out_code = nk_enc_decode_mbc(enc, bytes, bytes_end);
  *out_width = (size_t)width;
  return NK_SUCCESS;
}

// Bumps the visited-set generation, recovering from (unlikely) wraparound.
static void next_token(vm_t* vm) {
  if (vm->token == UINT32_MAX) {
    memset(
      vm->visited,
      0,
      (size_t)(vm->program->num_check_ids == 0 ? 1 : vm->program->num_check_ids) * sizeof(uint32_t)
    );
    vm->token = 0;
  }
  vm->token++;
}

// ============================================================================
//
// Fast Thompson NFA bitset path (no_caps boolean match, small programs):
//
// ============================================================================

// Return the index of the lowest set bit in `lsb` (which must be a power of
// two, i.e. a single isolated bit).  Uses the compiler built-in when
// available (maps to BSF/TZCNT on x86, CLZ on ARM), otherwise a portable
// right-shift loop.
static uint32_t bitset_lsb_index(uint64_t lsb) {
#if defined(__GNUC__) || defined(__clang__)
  return (uint32_t)__builtin_ctzll((unsigned long long)lsb);
#else
  uint32_t idx = 0;
  uint64_t tmp = lsb;
  while (tmp > 1u) {
    tmp >>= 1;
    idx++;
  }
  return idx;
#endif
}

// Compute the next active-state bitmask by advancing `active` over `curr_code`.
// Does NOT add the initial mask (the caller handles non-anchored re-injection).
static uint64_t bitset_transition(const nk_program_t* program, uint64_t active, uint32_t curr_code) {
  uint64_t next = 0;
  uint64_t bits = active;
  while (bits != 0) {
    uint64_t lsb = bits & (uint64_t)(-(int64_t)bits);
    bits ^= lsb;
    uint32_t idx = bitset_lsb_index(lsb);
    if (state_matches_code(program, &program->states[idx], curr_code)) {
      next |= program->goto_mask[idx];
    }
  }
  return next;
}

// Compute `transition(active, curr_code)` using the lazy DFA cache when the
// character is ASCII.  Falls back to `bitset_transition` on a cache miss and
// stores the result for future calls.  Non-ASCII characters always bypass the
// cache (UTF-8 continuation bytes are filtered at the call site).
//
// The cache lives in `program->lazy_dfa`, which is mutable even when accessed
// via a `const nk_program_t*` pointer: the pointer field itself is read-only,
// but the heap allocation it points to is not.
static uint64_t bitset_transition_cached(const nk_program_t* program, uint64_t active, uint32_t curr_code) {
  nk_lazy_dfa_t* ld = program->lazy_dfa;
  if (ld == NULL || curr_code >= 128u) {
    return bitset_transition(program, active, curr_code);
  }

  uint8_t cbyte = (uint8_t)curr_code;

  // Reset the cache when 75% full to prevent probe-chain degradation.
  if (ld->fill >= (NK_LAZY_DFA_SLOTS * 3u / 4u)) {
    memset(ld->slots, 0, sizeof(ld->slots));
    ld->fill = 0u;
  }

  // Fibonacci hash of the (state_set, char) pair for good slot distribution.
  uint32_t h = (uint32_t)((active * 11400714819323198485ULL ^ (uint64_t)cbyte) &
               (uint64_t)(NK_LAZY_DFA_SLOTS - 1u));

  for (uint32_t probe = 0; probe < NK_LAZY_DFA_SLOTS; probe++) {
    nk_lazy_dfa_slot_t* slot = &ld->slots[(h + probe) & (NK_LAZY_DFA_SLOTS - 1u)];
    if (!slot->occupied) {
      // Cache miss: compute, store, and return.
      uint64_t next = bitset_transition(program, active, curr_code);
      slot->state_key = active;
      slot->next_key  = next;
      slot->char_byte = cbyte;
      slot->occupied  = 1;
      ld->fill++;
      return next;
    }
    if (slot->state_key == active && slot->char_byte == cbyte) {
      return slot->next_key;  // Cache hit.
    }
  }
  // Unreachable after the 75%-full reset guard; kept as a safety fallback.
  return bitset_transition(program, active, curr_code);
}

static nk_error_t search_impl_bitset(
  const nk_program_t* program,
  const uint8_t* subject_bytes,
  const uint8_t* subject_bytes_end,
  size_t start_offset
) {
  const nk_encoding_t* enc = program->enc;

  uint64_t active = program->initial_mask & ~NK_BITSET_MATCH_BIT;
  if (program->initial_mask & NK_BITSET_MATCH_BIT) {
    return NK_SUCCESS;  // empty pattern matches at start
  }
  if (active == 0 && program->is_anchored) {
    return NK_NO_MATCH;
  }

  size_t pos = start_offset;
  uint32_t curr_code;
  size_t curr_width;
  nk_error_t err = decode_char(enc, subject_bytes + pos, subject_bytes_end, &curr_code, &curr_width);
  if (err != NK_SUCCESS) {
    return err;
  }

  while (curr_code != VM_NO_CHAR) {
    uint64_t next = bitset_transition_cached(program, active, curr_code);

    if (next & NK_BITSET_MATCH_BIT) {
      return NK_SUCCESS;
    }

    if (!program->is_anchored) {
      // Re-inject threads for the next start position.
      next |= program->initial_mask & ~NK_BITSET_MATCH_BIT;
    }

    active = next;

    pos += curr_width;
    err = decode_char(enc, subject_bytes + pos, subject_bytes_end, &curr_code, &curr_width);
    if (err != NK_SUCCESS) {
      return err;
    }
  }

  return NK_NO_MATCH;
}

static nk_error_t search_impl(
  const nk_program_t* program,
  const uint8_t* subject_bytes,
  const uint8_t* subject_bytes_end,
  size_t start_offset,
  bool no_caps,
  nk_region_t* out_region
) {
  if (program == NULL || subject_bytes == NULL || subject_bytes_end < subject_bytes) {
    return NK_ERR_INTERNAL_ERROR;
  }

  const nk_encoding_t* enc = program->enc;
  size_t subject_len = (size_t)(subject_bytes_end - subject_bytes);
  if (start_offset > subject_len) {
    return NK_NO_MATCH;
  }

  // Required-byte prefilter: if the pattern mandates an ASCII byte in every
  // match and that byte is absent from the search window, exit immediately.
  // Only active when no literal prefix is set (the prefix already implies
  // this check through vm_memmem).
  if (program->has_required_byte) {
    size_t scan_len = subject_len > start_offset ? subject_len - start_offset : 0u;
    if (memchr(subject_bytes + start_offset, (int)program->required_byte, scan_len) == NULL) {
      return NK_NO_MATCH;
    }
  }

  // Pre-scan: if the program has an ASCII literal prefix, use vm_memmem to
  // find the first candidate start position. This avoids running the epsilon
  // closure at every character just to fail immediately — the biggest win is
  // for subjects that do not contain the prefix at all (immediate no-match).
  if (program->literal_prefix_len > 0 && !program->is_anchored) {
    size_t scan_len = subject_len > start_offset ? subject_len - start_offset : 0;
    const uint8_t* found =
      vm_memmem(subject_bytes + start_offset, scan_len, program->literal_prefix_bytes, program->literal_prefix_len);
    if (found == NULL) {
      return NK_NO_MATCH;
    }
    start_offset = (size_t)(found - subject_bytes);
  }

  // Multi-literal alternation pre-scan: if the pattern is a pure alternation
  // of ASCII literals (e.g., `foo|bar|baz`), scan for the earliest occurrence
  // of any alternative.  Returns NK_NO_MATCH immediately when none is found.
  if (program->alt_literal_count > 0u) {
    size_t scan_len = subject_len > start_offset ? subject_len - start_offset : 0u;
    const uint8_t* scan_base = subject_bytes + start_offset;
    const uint8_t* earliest = NULL;
    for (size_t i = 0; i < program->alt_literal_count; i++) {
      const uint8_t* found = vm_memmem(scan_base, scan_len,
                                        program->alt_literal_bytes[i],
                                        program->alt_literal_lens[i]);
      if (found != NULL && (earliest == NULL || found < earliest)) {
        earliest = found;
      }
    }
    if (earliest == NULL) return NK_NO_MATCH;
    start_offset = (size_t)(earliest - subject_bytes);
  }

  // Fast Thompson NFA bitset path: no allocation, O(active_states) per char.
  if (no_caps && out_region == NULL && program->goto_mask != NULL) {
    return search_impl_bitset(program, subject_bytes, subject_bytes_end, start_offset);
  }

  vm_t vm;
  memset(&vm, 0, sizeof(vm));
  vm.program = program;
  vm.num_caps = (size_t)program->num_capture_groups + 1;
  vm.no_caps = no_caps;
  vm.start_offset = start_offset;

  size_t visited_len = program->num_check_ids == 0 ? 1 : (size_t)program->num_check_ids;
  vm.visited = (uint32_t*)calloc(visited_len, sizeof(uint32_t));
  if (vm.visited == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  thread_list_t threads;
  thread_list_t next_threads;
  memset(&threads, 0, sizeof(threads));
  memset(&next_threads, 0, sizeof(next_threads));

  nk_error_t err = NK_SUCCESS;

  // Walk the prefix before `start_offset` to find the previous character for
  // assertions, validating both the encoding and the boundary on the way.
  uint32_t prev_code = VM_NO_CHAR;
  {
    const uint8_t* bytes = subject_bytes;
    const uint8_t* start_bytes = subject_bytes + start_offset;
    while (bytes < start_bytes) {
      int8_t width = nk_enc_scan_mbc_width(enc, bytes, subject_bytes_end);
      if (width == 0 || bytes + (width > 0 ? width : 0) > start_bytes) {
        err = NK_ERR_INVALID_BYTE_SEQUENCE;
        goto done;
      }
      if (width < 0) {
        err = NK_ERR_INCOMPLETE_BYTE_SEQUENCE;
        goto done;
      }
      prev_code = nk_enc_decode_mbc(enc, bytes, subject_bytes_end);
      bytes += width;
    }
  }

  size_t pos = start_offset;
  uint32_t curr_code;
  size_t curr_width;
  err = decode_char(enc, subject_bytes + pos, subject_bytes_end, &curr_code, &curr_width);
  if (err != NK_SUCCESS) {
    goto done;
  }
  uint32_t next_code;
  size_t next_width;
  err = decode_char(enc, subject_bytes + pos + curr_width, subject_bytes_end, &next_code, &next_width);
  if (err != NK_SUCCESS) {
    goto done;
  }

  // Seed the thread list at the starting position.
  next_token(&vm);
  vm.closure_pos = pos;
  vm.prev_code = prev_code;
  vm.curr_code = curr_code;
  vm.next_code = next_code;
  {
    caps_t* caps = no_caps ? NO_CAPS_PTR : caps_new(vm.num_caps);
    if (caps == NULL) {
      err = NK_ERR_MEMORY_ALLOCATION_FAILED;
      goto done;
    }
    err = closure(&vm, program->initial_state, pos, caps, &threads);
    if (err != NK_SUCCESS) {
      goto done;
    }
  }

  // For anchored patterns (\A), no new start threads will be injected past
  // position 0, so we can exit as soon as the active thread list is empty.
  while (curr_code != VM_NO_CHAR && (threads.len > 0 || !program->is_anchored) && !(vm.has_match && threads.len == 0)) {
    // ASCII run scan: when exactly one thread is at a single-character state
    // (CHAR_CLASS or CODE) and the current character matches, fast-advance
    // through the entire consecutive run to replace O(N) epsilon closures with
    // O(1). CODE scan is guarded to loop-head states only (next is epsilon).
    if (threads.len == 1 && curr_code < 0x80u && !vm.has_match) {
      const nk_vm_state_t* scan_state = &program->states[threads.items[0].state_index];
      size_t scan = 0;

      if (scan_state->op == NK_VM_OP_CHAR_CLASS) {
        const nk_vm_char_class_t* cc = &program->char_classes[scan_state->char_class_index];
        if ((cc->ascii_bits[curr_code >> 6] & ((uint64_t)1u << (curr_code & 63u))) != 0u) {
          scan = pos + 1;
          while (scan < subject_len && subject_bytes[scan] < 0x80u &&
                 (cc->ascii_bits[subject_bytes[scan] >> 6] & ((uint64_t)1u << (subject_bytes[scan] & 63u))) != 0u) {
            scan++;
          }
        }
      } else if (scan_state->op == NK_VM_OP_CODE && curr_code == scan_state->code) {
        // Only scan a consecutive run when the state's successor is an epsilon
        // (non-consuming) state, indicating this is a loop-head (e.g., `a+`).
        // If the next state is a consuming op, this CODE is part of a sequence
        // like "foo" and scanning would incorrectly consume the wrong literal.
        uint32_t next_idx = scan_state->next;
        if (next_idx < (uint32_t)program->states_len) {
          nk_vm_op_t next_op = program->states[next_idx].op;
          if (next_op != NK_VM_OP_CODE && next_op != NK_VM_OP_CHAR_CLASS &&
              next_op != NK_VM_OP_DOT && next_op != NK_VM_OP_MATCH) {
            uint8_t code_byte = (uint8_t)scan_state->code;
            scan = pos + 1;
            while (scan < subject_len && subject_bytes[scan] == code_byte) {
              scan++;
            }
          }
        }
      }

      if (scan > pos + 1) {
        pos = scan - 1;
        curr_code = (uint32_t)subject_bytes[pos];
        curr_width = 1;
        err = decode_char(enc, subject_bytes + pos + 1, subject_bytes_end, &next_code, &next_width);
        if (err != NK_SUCCESS) {
          goto done;
        }
      }
    }

    size_t advance_pos = pos + curr_width;

    // The character window after consuming the current character.
    uint32_t after_code = VM_NO_CHAR;
    size_t after_width = 0;
    if (next_code != VM_NO_CHAR) {
      err = decode_char(enc, subject_bytes + advance_pos + next_width, subject_bytes_end, &after_code, &after_width);
      if (err != NK_SUCCESS) {
        goto done;
      }
    }

    next_token(&vm);
    vm.closure_pos = advance_pos;
    vm.prev_code = curr_code;
    vm.curr_code = next_code;
    vm.next_code = after_code;

    // Advance every thread that matches the current character, in priority
    // order. Once a match is recorded, the remaining (lower-priority)
    // threads can never win and are dropped.
    for (size_t i = 0; i < threads.len; i++) {
      thread_t* thread = &threads.items[i];
      const nk_vm_state_t* state = &program->states[thread->state_index];
      if (state_matches_code(program, state, curr_code)) {
        uint64_t version_before = vm.match_version;
        caps_t* caps = thread->caps;
        thread->caps = NULL;
        err = closure(&vm, state->next, thread->keep_pos, caps, &next_threads);
        if (err != NK_SUCCESS) {
          goto done;
        }
        if (vm.match_version != version_before) {
          for (size_t j = i + 1; j < threads.len; j++) {
            caps_unref(threads.items[j].caps);
            threads.items[j].caps = NULL;
          }
          break;
        }
      } else {
        caps_unref(thread->caps);
        thread->caps = NULL;
      }
    }
    threads.len = 0;

    // Non-anchored search: while no match has been found, also try starting
    // at the new position, with the lowest priority. Skipped for \A-anchored
    // patterns because the start assertion will never pass after position 0.
    if (!vm.has_match && !program->is_anchored) {
      caps_t* caps = no_caps ? NO_CAPS_PTR : caps_new(vm.num_caps);
      if (caps == NULL) {
        err = NK_ERR_MEMORY_ALLOCATION_FAILED;
        goto done;
      }
      err = closure(&vm, program->initial_state, advance_pos, caps, &next_threads);
      if (err != NK_SUCCESS) {
        goto done;
      }
    }

    thread_list_t tmp = threads;
    threads = next_threads;
    next_threads = tmp;

    pos = advance_pos;
    curr_code = next_code;
    curr_width = next_width;
    next_code = after_code;
    next_width = after_width;
  }

done:
  if (err == NK_SUCCESS && vm.has_match) {
    if (out_region != NULL && out_region->caps != NULL) {
      size_t copied_caps = out_region->num_caps < vm.num_caps ? out_region->num_caps : vm.num_caps;
      memcpy(out_region->caps, vm.match_caps->data, 2 * copied_caps * sizeof(size_t));
      if (copied_caps > 0) {
        out_region->caps[0] = vm.match_keep_pos;
      }
    }
  } else if (err == NK_SUCCESS) {
    err = NK_NO_MATCH;
  }

  thread_list_free(&threads);
  thread_list_free(&next_threads);
  drain_stack(&vm);
  free(vm.stack);
  caps_unref(vm.match_caps);
  free(vm.visited);
  return err;
}

nk_error_t nk_program_search(
  const nk_program_t* program,
  const uint8_t* subject_bytes,
  const uint8_t* subject_bytes_end,
  size_t start_offset,
  nk_region_t* out_region
) {
  return search_impl(program, subject_bytes, subject_bytes_end, start_offset, false, out_region);
}

nk_error_t nk_program_search_boolean(
  const nk_program_t* program,
  const uint8_t* subject_bytes,
  const uint8_t* subject_bytes_end,
  size_t start_offset
) {
  return search_impl(program, subject_bytes, subject_bytes_end, start_offset, true, NULL);
}
