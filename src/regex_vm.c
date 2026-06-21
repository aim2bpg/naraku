/**
 * @file regex_vm.c
 */

#include <naraku_regex.h>
#include <naraku_regex_internal.h>

#include <stdlib.h>
#include <string.h>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

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

#if defined(__SSE2__)
// Returns the number of leading bytes at `p` (capped at `limit`) that fall
// within one of `ranges[0..range_count)` (inclusive ASCII byte ranges).
// Scans 16 bytes per iteration using SSE2 unsigned min/max range tests
// (`_mm_min_epu8`/`_mm_max_epu8`, baseline SSE2 — no `-march` flag needed).
// Stops SIMD scanning at the first chunk containing a non-member byte and
// lets the scalar tail loop below pin down the exact boundary; this keeps
// the function simple (no bit-scan of the comparison mask is needed).
static size_t simd_ascii_range_run(const uint8_t* p, size_t limit, const uint8_t ranges[][2], uint8_t range_count) {
  size_t i = 0;
  while (i + 16u <= limit) {
    __m128i bytes = _mm_loadu_si128((const __m128i*)(p + i));
    __m128i in_any = _mm_setzero_si128();
    for (uint8_t r = 0; r < range_count; r++) {
      __m128i lo_v = _mm_set1_epi8((char)ranges[r][0]);
      __m128i hi_v = _mm_set1_epi8((char)ranges[r][1]);
      __m128i clamped = _mm_max_epu8(_mm_min_epu8(bytes, hi_v), lo_v);
      in_any = _mm_or_si128(in_any, _mm_cmpeq_epi8(clamped, bytes));
    }
    if ((unsigned)_mm_movemask_epi8(in_any) != 0xFFFFu) {
      break;  // a non-member byte is in this chunk; fall through to the scalar tail.
    }
    i += 16u;
  }
  while (i < limit) {
    uint8_t byte = p[i];
    bool member = false;
    for (uint8_t r = 0; r < range_count; r++) {
      if (byte >= ranges[r][0] && byte <= ranges[r][1]) {
        member = true;
        break;
      }
    }
    if (!member) {
      break;
    }
    i++;
  }
  return i;
}
#endif

// Returns the number of leading bytes at `p` (capped at `limit`) that are
// members of `cc`'s ASCII subset (equivalent to repeatedly testing
// `cc->ascii_lookup[byte] != 0`). Uses the SSE2 range scan above when `cc`
// qualifies (`simd_range_count > 0` — set at compile time in
// `program_add_char_class` for classes that decompose into a handful of
// contiguous byte ranges, e.g. `\d`, `\w`, `[a-zA-Z0-9]`); falls back to the
// plain scalar loop otherwise, which is always correct.
static size_t ascii_class_run_length(const nk_vm_char_class_t* cc, const uint8_t* p, size_t limit) {
#if defined(__SSE2__)
  if (cc->simd_range_count > 0u) {
    return simd_ascii_range_run(p, limit, cc->simd_ranges, cc->simd_range_count);
  }
#endif
  size_t i = 0;
  while (i < limit && cc->ascii_lookup[p[i]] != 0u) {
    i++;
  }
  return i;
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

// Forward struct declaration for possessive pending list (functions defined later).
typedef struct {
  size_t target_pos;
  uint32_t state_index;
  size_t keep_pos;
  caps_t* caps;
} poss_pending_entry_t;

typedef struct {
  poss_pending_entry_t* items;
  size_t len;
  size_t cap;
} poss_pending_t;

// Forward declaration (function body is after br_deferred helpers).
static nk_error_t
poss_pending_push(poss_pending_t* p, size_t target_pos, uint32_t state_index, size_t keep_pos, caps_t* caps);

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

  // Subject byte range, needed by the LOOKAROUND/POSSESSIVE handlers in closure().
  const uint8_t* subject_bytes;
  const uint8_t* subject_bytes_end;

  // Possessive-quantifier pending list: entries injected by NK_VM_OP_POSSESSIVE
  // in closure() that need to fire at a future advance_pos.
  poss_pending_t poss_pending;

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
  if (code < 0x80u) {
    return char_class->ascii_lookup[code] != 0u;
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
// `code` is never `VM_NO_CHAR` here. Declared in naraku_regex_internal.h —
// shared with src/regex_compile.c (compute_first_byte_table).
bool state_matches_code(const nk_program_t* program, const nk_vm_state_t* state, uint32_t code) {
  switch (state->op) {
    case NK_VM_OP_CODE:
      if (state->is_ignore_case) {
        if ((state->fold_flags & NK_FOLD_ASCII_ONLY) != 0) {
          if (code < 0x80u && state->code < 0x80u) {
            uint32_t fc = (code >= 'A' && code <= 'Z') ? (code | 0x20u) : code;
            uint32_t fp = (state->code >= 'A' && state->code <= 'Z') ? (state->code | 0x20u) : state->code;
            return fc == fp;
          }
        } else {
          uint32_t fi[NK_ENC_MAX_FOLDED_CODES];
          uint32_t fp[NK_ENC_MAX_FOLDED_CODES];
          size_t fi_n = nk_enc_get_case_fold(program->enc, state->fold_flags, code, fi);
          size_t fp_n = nk_enc_get_case_fold(program->enc, state->fold_flags, state->code, fp);
          return fi_n == 1 && fp_n == 1 && fi[0] == fp[0];
        }
      }
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

      case NK_VM_OP_BACK_REF:
        // BACK_REF is a variable-width consumer: skip dedup because two threads
        // at the same BACK_REF state with different caps advance different amounts.
        err = thread_list_push(out_threads, item.state_index, item.keep_pos, item.caps);
        if (err != NK_SUCCESS) {
          caps_unref(item.caps);
          goto fail;
        }
        break;

      case NK_VM_OP_POSSESSIVE:
      {
        const nk_program_t* inner = vm->program->sub_programs[state->possessive_prog_idx];
        nk_region_t ir;
        nk_error_t ie = nk_region_init(&ir, inner->num_capture_groups);
        if (ie != NK_SUCCESS) {
          caps_unref(item.caps);
          err = ie;
          goto fail;
        }
        nk_error_t se = nk_program_search(inner, vm->subject_bytes, vm->subject_bytes_end, vm->closure_pos, &ir);
        bool poss_matched = (se == NK_SUCCESS && ir.caps[0] == vm->closure_pos);
        size_t poss_end = poss_matched ? ir.caps[1] : 0;
        if (se != NK_SUCCESS && se != NK_NO_MATCH) {
          nk_region_free(&ir);
          caps_unref(item.caps);
          err = se;
          goto fail;
        }
        nk_region_free(&ir);

        if (!poss_matched) {
          caps_unref(item.caps);
          break;
        }
        if (poss_end == vm->closure_pos) {
          // Zero-width match: epsilon transition (e.g., a*+ with no 'a' here).
          err = push_state(vm, state->next, item.keep_pos, item.epsilon_bits, item.caps);
          if (err != NK_SUCCESS) {
            goto fail;
          }
        } else {
          // Multi-byte advance: defer continuation to the main loop at poss_end.
          err = poss_pending_push(&vm->poss_pending, poss_end, state->next, item.keep_pos, item.caps);
          if (err != NK_SUCCESS) {
            caps_unref(item.caps);
            goto fail;
          }
        }
        break;
      }

      case NK_VM_OP_LOOKAROUND:
      {
        const nk_program_t* inner = vm->program->sub_programs[state->lookaround_prog_idx];
        nk_region_t inner_region;
        nk_error_t init_err = nk_region_init(&inner_region, inner->num_capture_groups);
        if (init_err != NK_SUCCESS) {
          caps_unref(item.caps);
          err = init_err;
          goto fail;
        }

        bool assertion_passed = false;
        if (state->lookaround_is_ahead) {
          // Lookahead: the sub-pattern must match starting exactly at closure_pos.
          nk_error_t search_err =
            nk_program_search(inner, vm->subject_bytes, vm->subject_bytes_end, vm->closure_pos, &inner_region);
          if (search_err == NK_SUCCESS && inner_region.caps[0] == vm->closure_pos) {
            assertion_passed = state->lookaround_is_positive;
          } else if (search_err == NK_NO_MATCH ||
                     (search_err == NK_SUCCESS && inner_region.caps[0] != vm->closure_pos)) {
            assertion_passed = !state->lookaround_is_positive;
          } else if (search_err != NK_SUCCESS) {
            nk_region_free(&inner_region);
            caps_unref(item.caps);
            err = search_err;
            goto fail;
          }
        } else {
          // Lookbehind: the sub-pattern must end exactly at closure_pos.
          // Try every possible start position from 0..closure_pos.
          bool found = false;
          for (size_t try_start = 0; try_start <= vm->closure_pos; try_start++) {
            for (size_t k = 0; k < inner_region.num_caps * 2; k++) {
              inner_region.caps[k] = NK_REGION_POS_NONE;
            }
            nk_error_t search_err =
              nk_program_search(inner, vm->subject_bytes, vm->subject_bytes_end, try_start, &inner_region);
            if (search_err == NK_SUCCESS && inner_region.caps[1] == vm->closure_pos) {
              found = true;
              break;
            }
            if (search_err != NK_SUCCESS && search_err != NK_NO_MATCH) {
              nk_region_free(&inner_region);
              caps_unref(item.caps);
              err = search_err;
              goto fail;
            }
          }
          assertion_passed = found == state->lookaround_is_positive;
        }

        nk_region_free(&inner_region);

        if (assertion_passed) {
          err = push_state(vm, state->next, item.keep_pos, item.epsilon_bits, item.caps);
          if (err != NK_SUCCESS) {
            goto fail;
          }
        } else {
          caps_unref(item.caps);
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

// Decodes the code point that ends at byte position `pos` in `subject_bytes`.
// Scans backward up to NK_ENC_MAX_MBC_WIDTH bytes to find a valid character.
// Returns VM_NO_CHAR if pos == 0 or no valid boundary is found.
static uint32_t decode_code_before(const nk_encoding_t* enc, const uint8_t* subject_bytes, size_t pos) {
  if (pos == 0) {
    return VM_NO_CHAR;
  }
  size_t max_back = pos < (size_t)NK_ENC_MAX_MBC_WIDTH ? pos : (size_t)NK_ENC_MAX_MBC_WIDTH;
  for (size_t back = 1; back <= max_back; back++) {
    const uint8_t* p = subject_bytes + pos - back;
    const uint8_t* end = subject_bytes + pos;
    int8_t w = nk_enc_scan_mbc_width(enc, p, end);
    if (w > 0 && (size_t)w == back) {
      return nk_enc_decode_mbc(enc, p, end);
    }
  }
  return VM_NO_CHAR;
}

// Compares the captured string (subject[cap_begin..cap_end)) against the subject
// starting at `match_pos`. Sets `*out_matched` and `*out_match_len` (bytes consumed
// from subject at match_pos). Handles case-insensitive folding, including full-fold
// (CF2: fold("ß")=[s,s] matches "ss" and vice versa).
static nk_error_t back_ref_match(
  const nk_encoding_t* enc,
  const uint8_t* subject,
  size_t subject_len,
  size_t match_pos,
  size_t cap_begin,
  size_t cap_end,
  bool is_ignore_case,
  nk_fold_flag_t fold_flags,
  bool* out_matched,
  size_t* out_match_len
) {
  size_t cap_len = cap_end - cap_begin;

  if (cap_len == 0) {
    *out_matched = true;
    *out_match_len = 0;
    return NK_SUCCESS;
  }

  if (!is_ignore_case) {
    if (match_pos + cap_len > subject_len) {
      *out_matched = false;
      return NK_SUCCESS;
    }
    *out_matched = (memcmp(subject + cap_begin, subject + match_pos, cap_len) == 0);
    if (*out_matched) {
      *out_match_len = cap_len;
    }
    return NK_SUCCESS;
  }

  // Case-insensitive: fold both sides to code-point sequences and compare.
  // Uses a pair of small queues (each at most NK_ENC_MAX_FOLDED_CODES codes) to
  // handle 1-to-N fold expansion without dynamic allocation.
  const uint8_t* cap_ptr = subject + cap_begin;
  const uint8_t* cap_end_ptr = subject + cap_end;
  const uint8_t* subj_ptr = subject + match_pos;
  const uint8_t* subj_end_ptr = subject + subject_len;

  uint32_t cap_q[NK_ENC_MAX_FOLDED_CODES];
  size_t cap_q_len = 0;
  size_t cap_q_pos = 0;
  uint32_t subj_q[NK_ENC_MAX_FOLDED_CODES];
  size_t subj_q_len = 0;
  size_t subj_q_pos = 0;

  while (true) {
    bool cap_done = (cap_ptr >= cap_end_ptr && cap_q_pos >= cap_q_len);
    if (cap_done) {
      *out_matched = (subj_q_pos >= subj_q_len);
      if (*out_matched) {
        *out_match_len = (size_t)(subj_ptr - (subject + match_pos));
      }
      return NK_SUCCESS;
    }

    if (cap_q_pos >= cap_q_len) {
      int8_t cap_w = nk_enc_scan_mbc_width(enc, cap_ptr, cap_end_ptr);
      if (cap_w <= 0) {
        return NK_ERR_INVALID_BYTE_SEQUENCE;
      }
      uint32_t cap_code = nk_enc_decode_mbc(enc, cap_ptr, cap_end_ptr);
      cap_ptr += (size_t)cap_w;
      cap_q_pos = 0;
      cap_q_len = nk_enc_get_case_fold(enc, fold_flags, cap_code, cap_q);
    }

    if (subj_q_pos >= subj_q_len) {
      if (subj_ptr >= subj_end_ptr) {
        *out_matched = false;
        return NK_SUCCESS;
      }
      int8_t subj_w = nk_enc_scan_mbc_width(enc, subj_ptr, subj_end_ptr);
      if (subj_w <= 0) {
        return NK_ERR_INVALID_BYTE_SEQUENCE;
      }
      uint32_t subj_code = nk_enc_decode_mbc(enc, subj_ptr, subj_end_ptr);
      subj_ptr += (size_t)subj_w;
      subj_q_pos = 0;
      subj_q_len = nk_enc_get_case_fold(enc, fold_flags, subj_code, subj_q);
    }

    if (cap_q[cap_q_pos] != subj_q[subj_q_pos]) {
      *out_matched = false;
      return NK_SUCCESS;
    }
    cap_q_pos++;
    subj_q_pos++;
  }
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
// This section is `match?`'s hot path: each NFA state set is packed into a
// 128-bit bitmask (`nk_bitset128_t`) instead of a thread list, so there is no
// per-character allocation and no capture-boundary bookkeeping.
//
// Two lazy-DFA transition caches exist below (`bitset_transition_cached_narrow`
// and `bitset_transition_cached`) because one cache-slot layout can't serve
// both small and large state counts well — see nk_lazy_dfa_slot_narrow_t in
// naraku_regex.h for the cache-locality reason.
//
// `search_impl_bitset` additionally detects, after each cache lookup, when a
// repeated byte leaves the active-state set unchanged (a fixed point) and
// scans the rest of that run directly, skipping the cache entirely for
// patterns like `a+b` — see docs/ja/naraku_vm.md §6.3 for why this must
// happen after the lookup rather than before it.
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
static nk_bitset128_t bitset_transition(const nk_program_t* program, nk_bitset128_t active, uint32_t curr_code) {
  nk_bitset128_t next = NK_BITSET128_ZERO;

  uint64_t bits = active.lo;
  while (bits != 0) {
    uint64_t lsb = bits & (uint64_t)(-(int64_t)bits);
    bits ^= lsb;
    uint32_t idx = bitset_lsb_index(lsb);
    if (state_matches_code(program, &program->states[idx], curr_code)) {
      next = nk_bitset128_or(next, program->goto_mask[idx]);
    }
  }
  bits = active.hi;
  while (bits != 0) {
    uint64_t lsb = bits & (uint64_t)(-(int64_t)bits);
    bits ^= lsb;
    uint32_t idx = 64u + bitset_lsb_index(lsb);
    if (state_matches_code(program, &program->states[idx], curr_code)) {
      next = nk_bitset128_or(next, program->goto_mask[idx]);
    }
  }
  return next;
}

// Bit 63 of a narrow-cache `next_key` is repurposed to record the MATCH bit
// (`nk_bitset128_t::hi`'s only possible non-zero bit for a <=63-state
// program). Real state indices for such programs are always <= 62, so bit 63
// of the `.lo` word is otherwise unused and safe to borrow here.
#define NK_NARROW_MATCH_BIT ((uint64_t)1u << 63)

// Narrow-cache variant of bitset_transition_cached, used when
// program->lazy_dfa_narrow != NULL (states_len <= 63). Same hashing/probing
// logic as the wide path below, keyed on a single uint64_t instead of
// nk_bitset128_t. See nk_lazy_dfa_slot_narrow_t in naraku_regex.h for why
// this smaller layout exists. Deliberately not merged with the wide path via
// a shared macro/helper: the whole point is each one's key type matches its
// slot's memory layout exactly, and abstracting that away would either lose
// the size win or hurt readability.
//
// `active` passed in here never has its MATCH bit set (callers return as
// soon as they observe it — see search_impl_bitset), but the cached `next`
// value can, so the MATCH bit is folded into bit 63 of `next_key` on store
// and unfolded back into `.hi` on a cache hit.
static nk_bitset128_t
bitset_transition_cached_narrow(const nk_program_t* program, nk_bitset128_t active, uint32_t curr_code) {
  nk_lazy_dfa_narrow_t* ld = program->lazy_dfa_narrow;
  uint8_t cbyte = (uint8_t)curr_code;

  if (ld->fill >= (NK_LAZY_DFA_SLOTS * 3u / 4u)) {
    memset(ld->slots, 0, sizeof(ld->slots));
    ld->fill = 0u;
  }

  uint64_t h64 = (active.lo * 11400714819323198485ULL) ^ (uint64_t)cbyte;
  uint32_t h = (uint32_t)(h64 & (uint64_t)(NK_LAZY_DFA_SLOTS - 1u));

  for (uint32_t probe = 0; probe < NK_LAZY_DFA_SLOTS; probe++) {
    nk_lazy_dfa_slot_narrow_t* slot = &ld->slots[(h + probe) & (NK_LAZY_DFA_SLOTS - 1u)];
    if (!slot->occupied) {
      nk_bitset128_t next = bitset_transition(program, active, curr_code);
      slot->state_key = active.lo;
      slot->next_key = next.lo | (nk_bitset128_has_match_bit(next) ? NK_NARROW_MATCH_BIT : 0u);
      slot->char_byte = cbyte;
      slot->occupied = 1;
      ld->fill++;
      return next;
    }
    if (slot->state_key == active.lo && slot->char_byte == cbyte) {
      nk_bitset128_t next;
      next.hi = (slot->next_key & NK_NARROW_MATCH_BIT) ? NK_NARROW_MATCH_BIT : 0u;
      next.lo = slot->next_key & ~NK_NARROW_MATCH_BIT;
      return next;  // Cache hit.
    }
  }
  // Unreachable after the 75%-full reset guard; kept as a safety fallback.
  return bitset_transition(program, active, curr_code);
}

// Compute `transition(active, curr_code)` using the lazy DFA cache when the
// character is ASCII.  Falls back to `bitset_transition` on a cache miss and
// stores the result for future calls.  Non-ASCII characters always bypass the
// cache (UTF-8 continuation bytes are filtered at the call site).
//
// The cache lives in `program->lazy_dfa`/`lazy_dfa_narrow`, which are mutable
// even when accessed via a `const nk_program_t*` pointer: the pointer field
// itself is read-only, but the heap allocation it points to is not.
static nk_bitset128_t bitset_transition_cached(const nk_program_t* program, nk_bitset128_t active, uint32_t curr_code) {
  if (curr_code >= 128u) {
    return bitset_transition(program, active, curr_code);
  }

  if (program->lazy_dfa_narrow != NULL) {
    return bitset_transition_cached_narrow(program, active, curr_code);
  }

  nk_lazy_dfa_t* ld = program->lazy_dfa;
  if (ld == NULL) {
    return bitset_transition(program, active, curr_code);
  }

  uint8_t cbyte = (uint8_t)curr_code;

  // Reset the cache when 75% full to prevent probe-chain degradation.
  if (ld->fill >= (NK_LAZY_DFA_SLOTS * 3u / 4u)) {
    memset(ld->slots, 0, sizeof(ld->slots));
    ld->fill = 0u;
  }

  // Fibonacci hash of the (state_set, char) pair for good slot distribution.
  // Both words of the 128-bit key are mixed in so lo/hi collisions don't alias.
  uint64_t mixed = (active.lo * 11400714819323198485ULL) ^ (active.hi * 14029467366897019727ULL) ^ (uint64_t)cbyte;
  uint32_t h = (uint32_t)(mixed & (uint64_t)(NK_LAZY_DFA_SLOTS - 1u));

  for (uint32_t probe = 0; probe < NK_LAZY_DFA_SLOTS; probe++) {
    nk_lazy_dfa_slot_t* slot = &ld->slots[(h + probe) & (NK_LAZY_DFA_SLOTS - 1u)];
    if (!slot->occupied) {
      // Cache miss: compute, store, and return.
      nk_bitset128_t next = bitset_transition(program, active, curr_code);
      slot->state_key = active;
      slot->next_key = next;
      slot->char_byte = cbyte;
      slot->occupied = 1;
      ld->fill++;
      return next;
    }
    if (nk_bitset128_eq(slot->state_key, active) && slot->char_byte == cbyte) {
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

  nk_bitset128_t start_active = nk_bitset128_andnot(program->initial_mask, NK_BITSET128_MATCH_BIT);
  nk_bitset128_t active = start_active;
  if (nk_bitset128_has_match_bit(program->initial_mask)) {
    return NK_SUCCESS;  // empty pattern matches at start
  }
  if (nk_bitset128_is_zero(active) && program->is_anchored) {
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
    // First-byte jump: when the NFA is in its initial (reset) state and the
    // current byte cannot begin a match, scan forward to the next candidate.
    // This replaces O(N) per-position closures with a fast byte scan for
    // patterns like `\d{n}` on non-matching inputs (e.g., `not-a-date`).
    if (!program->is_anchored && program->first_byte_table_valid && nk_bitset128_eq(active, start_active) &&
        curr_code < 128u && program->first_byte_table[(uint8_t)curr_code] == 0u) {
      const uint8_t* p = subject_bytes + pos + 1u;
      while (p < subject_bytes_end && *p < 128u && program->first_byte_table[*p] == 0u) {
        p++;
      }
      if (p >= subject_bytes_end) {
        return NK_NO_MATCH;
      }
      pos = (size_t)(p - subject_bytes);
      curr_code = (uint32_t)*p;
      curr_width = 1u;
    }

    nk_bitset128_t next = bitset_transition_cached(program, active, curr_code);

    if (nk_bitset128_has_match_bit(next)) {
      return NK_SUCCESS;
    }

    if (!program->is_anchored) {
      // Re-inject threads for the next start position.
      next = nk_bitset128_or(next, start_active);
    }

    // Single-byte repetition shortcut: `next` is already computed above (no
    // extra bit-scan), so checking whether it is a fixed point (next ==
    // active) is just two uint64_t comparisons. When it holds, consuming the
    // same byte again is guaranteed to reproduce the same active set
    // (bitset_transition is a pure function of (active, byte)), so the run
    // can be scanned directly instead of paying one lazy-DFA cache lookup
    // per repeated byte. ASCII-only (curr_width == 1) to keep the scan a
    // plain byte comparison. Safe even under case folding: it only ever
    // extends the run by the exact byte already observed, never claims
    // fold-equivalent bytes also continue it, so it cannot under- or
    // over-match. Design rationale for why this check must come after the
    // cache lookup rather than before: docs/ja/naraku_vm.md §6.3.
    if (curr_code < 128u && nk_bitset128_eq(next, active)) {
      uint8_t run_byte = (uint8_t)curr_code;
      size_t subject_len = (size_t)(subject_bytes_end - subject_bytes);
      size_t scan = pos + curr_width;
      while (scan < subject_len && subject_bytes[scan] == run_byte) {
        scan++;
      }
      pos = scan;
      err = decode_char(enc, subject_bytes + pos, subject_bytes_end, &curr_code, &curr_width);
      if (err != NK_SUCCESS) {
        return err;
      }
      continue;
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

// ============================================================================
//
// Deferred thread slots for BACK_REF variable-width consumers:
//
// ============================================================================

// A slot of threads deferred to a future position (produced by BACK_REF).
typedef struct {
  size_t target_pos;
  thread_list_t threads;
} br_slot_t;

typedef struct {
  br_slot_t* items;
  size_t len;
  size_t cap;
} br_deferred_t;

static void br_deferred_free(br_deferred_t* d) {
  for (size_t i = 0; i < d->len; i++) {
    thread_list_free(&d->items[i].threads);
  }
  free(d->items);
  d->items = NULL;
  d->len = 0;
  d->cap = 0;
}

// Pushes a thread into the slot at `target_pos`, creating a new slot if needed.
static nk_error_t
br_deferred_push(br_deferred_t* d, size_t target_pos, uint32_t state_index, size_t keep_pos, caps_t* caps) {
  // Find existing slot for target_pos
  for (size_t i = 0; i < d->len; i++) {
    if (d->items[i].target_pos == target_pos) {
      return thread_list_push(&d->items[i].threads, state_index, keep_pos, caps);
    }
  }
  // Create new slot
  if (d->len == d->cap) {
    size_t new_cap = d->cap == 0 ? 4 : d->cap * 2;
    br_slot_t* new_items = (br_slot_t*)realloc(d->items, new_cap * sizeof(br_slot_t));
    if (new_items == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    d->items = new_items;
    d->cap = new_cap;
  }
  br_slot_t* slot = &d->items[d->len++];
  memset(&slot->threads, 0, sizeof(slot->threads));
  slot->target_pos = target_pos;
  return thread_list_push(&slot->threads, state_index, keep_pos, caps);
}

// Extracts all threads for `pos` from the deferred list and injects them into
// `threads` (appended at the end). The slot is freed and removed.
static nk_error_t br_deferred_inject(br_deferred_t* d, size_t pos, thread_list_t* threads) {
  for (size_t i = 0; i < d->len; i++) {
    if (d->items[i].target_pos != pos) {
      continue;
    }
    thread_list_t* slot_threads = &d->items[i].threads;
    for (size_t j = 0; j < slot_threads->len; j++) {
      thread_t* t = &slot_threads->items[j];
      nk_error_t err = thread_list_push(threads, t->state_index, t->keep_pos, t->caps);
      if (err != NK_SUCCESS) {
        // Unref remaining threads in slot
        for (size_t k = j; k < slot_threads->len; k++) {
          caps_unref(slot_threads->items[k].caps);
        }
        slot_threads->len = 0;
        // Compact the slot list
        free(slot_threads->items);
        d->len--;
        if (i < d->len) {
          d->items[i] = d->items[d->len];
        }
        return err;
      }
      t->caps = NULL;  // transferred
    }
    free(slot_threads->items);
    slot_threads->items = NULL;
    slot_threads->len = 0;
    slot_threads->cap = 0;
    // Remove slot from list
    d->len--;
    if (i < d->len) {
      d->items[i] = d->items[d->len];
    }
    return NK_SUCCESS;
  }
  return NK_SUCCESS;
}

// ============================================================================
//
// Possessive quantifier pending list:
//
// ============================================================================

// An entry deferred to a future position by NK_VM_OP_POSSESSIVE in closure().
// When advance_pos reaches target_pos, closure(state_index) is called to inject
// the continuation into next_threads.
static nk_error_t
poss_pending_push(poss_pending_t* p, size_t target_pos, uint32_t state_index, size_t keep_pos, caps_t* caps) {
  if (p->len == p->cap) {
    size_t new_cap = p->cap == 0 ? 4 : p->cap * 2;
    poss_pending_entry_t* new_items = (poss_pending_entry_t*)realloc(p->items, new_cap * sizeof(poss_pending_entry_t));
    if (new_items == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    p->items = new_items;
    p->cap = new_cap;
  }
  poss_pending_entry_t e = {target_pos, state_index, keep_pos, caps};
  p->items[p->len++] = e;
  return NK_SUCCESS;
}

static void poss_pending_free(poss_pending_t* p) {
  for (size_t i = 0; i < p->len; i++) {
    caps_unref(p->items[i].caps);
  }
  free(p->items);
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

  // BACK_REF requires reading captures; disable the no-allocation fast path.
  if (program->has_back_refs) {
    no_caps = false;
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

    // Pure literal bypass: memmem confirmed the complete match — skip the NFA.
    if (program->is_pure_literal) {
      if (out_region != NULL && out_region->caps != NULL) {
        out_region->caps[0] = start_offset;
        out_region->caps[1] = start_offset + program->literal_prefix_len;
        for (size_t i = 2; i < 2 * out_region->num_caps; i++) {
          out_region->caps[i] = NK_REGION_POS_NONE;
        }
      }
      return NK_SUCCESS;
    }
  }

  // Multi-literal alternation pre-scan: if the pattern is a pure alternation
  // of ASCII literals (e.g., `foo|bar|baz`), scan for the earliest occurrence
  // of any alternative.  Returns NK_NO_MATCH immediately when none is found.
  if (program->alt_literal_count > 0u) {
    size_t scan_len = subject_len > start_offset ? subject_len - start_offset : 0u;
    const uint8_t* scan_base = subject_bytes + start_offset;
    const uint8_t* earliest = NULL;
    size_t earliest_len = 0;
    for (size_t i = 0; i < program->alt_literal_count; i++) {
      const uint8_t* found =
        vm_memmem(scan_base, scan_len, program->alt_literal_bytes[i], program->alt_literal_lens[i]);
      if (found != NULL && (earliest == NULL || found < earliest)) {
        earliest = found;
        earliest_len = program->alt_literal_lens[i];
      }
    }
    if (earliest == NULL) return NK_NO_MATCH;
    start_offset = (size_t)(earliest - subject_bytes);

    // Pure alternation bypass: the pre-scan already confirmed a complete literal
    // match for one of the branches — skip the NFA.  Tie-breaking at the same
    // position follows storage order (= left-to-right alternation priority).
    if (program->is_pure_alt_literal) {
      if (out_region != NULL && out_region->caps != NULL) {
        out_region->caps[0] = start_offset;
        out_region->caps[1] = start_offset + earliest_len;
      }
      return NK_SUCCESS;
    }
  }

  // Non-ASCII literal/alternation bypass: encoding-agnostic memmem scan over
  // the full byte sequences.  Handles pure literals and alternations whose
  // branches contain non-ASCII bytes (not covered by the ASCII-only paths above).
  if (program->full_alt_count > 0u) {
    size_t scan_len = subject_len > start_offset ? subject_len - start_offset : 0u;
    const uint8_t* scan_base = subject_bytes + start_offset;
    const uint8_t* earliest = NULL;
    size_t earliest_len = 0;
    for (size_t i = 0; i < program->full_alt_count; i++) {
      const uint8_t* found = vm_memmem(scan_base, scan_len, program->full_alt_bytes[i], program->full_alt_lens[i]);
      if (found != NULL && (earliest == NULL || found < earliest)) {
        earliest = found;
        earliest_len = program->full_alt_lens[i];
      }
    }
    if (earliest == NULL) return NK_NO_MATCH;
    if (out_region != NULL && out_region->caps != NULL) {
      out_region->caps[0] = (size_t)(earliest - subject_bytes);
      out_region->caps[1] = (size_t)(earliest - subject_bytes) + earliest_len;
    }
    return NK_SUCCESS;
  }

  // Pure char-class loop bypass: [X]+, \w+, \d+ etc. with no captures.
  // Scan ascii_lookup directly — the NFA is not needed to confirm a match.
  if (program->is_pure_char_class_plus) {
    const nk_vm_char_class_t* cc = &program->char_classes[program->pure_cc_index];
    const uint8_t* p = subject_bytes + start_offset;
    while (p < subject_bytes_end) {
      if (*p < 128u) {
        if (cc->ascii_lookup[*p] != 0u) {
          if (out_region != NULL && out_region->caps != NULL) {
            size_t ms = (size_t)(p - subject_bytes);
            const uint8_t* q = p + 1 + ascii_class_run_length(cc, p + 1, (size_t)(subject_bytes_end - (p + 1)));
            out_region->caps[0] = ms;
            out_region->caps[1] = (size_t)(q - subject_bytes);
            for (size_t i = 2; i < 2u * out_region->num_caps; i++) {
              out_region->caps[i] = NK_REGION_POS_NONE;
            }
          }
          return NK_SUCCESS;
        }
        p++;
      } else {
        int8_t sw = nk_enc_scan_mbc_width(program->enc, p, subject_bytes_end);
        size_t w = (sw > 0) ? (size_t)sw : 1u;
        uint32_t code = (sw > 0) ? nk_enc_decode_mbc(program->enc, p, subject_bytes_end) : 0xFFFFFFFFu;
        if (char_class_contains(cc, code)) {
          if (out_region != NULL && out_region->caps != NULL) {
            out_region->caps[0] = (size_t)(p - subject_bytes);
            out_region->caps[1] = (size_t)(p - subject_bytes) + w;
            for (size_t i = 2; i < 2u * out_region->num_caps; i++) {
              out_region->caps[i] = NK_REGION_POS_NONE;
            }
          }
          return NK_SUCCESS;
        }
        p += w;
      }
    }
    return NK_NO_MATCH;
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
  vm.subject_bytes = subject_bytes;
  vm.subject_bytes_end = subject_bytes_end;

  size_t visited_len = program->num_check_ids == 0 ? 1 : (size_t)program->num_check_ids;
  vm.visited = (uint32_t*)calloc(visited_len, sizeof(uint32_t));
  if (vm.visited == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  thread_list_t threads;
  thread_list_t next_threads;
  memset(&threads, 0, sizeof(threads));
  memset(&next_threads, 0, sizeof(next_threads));

  br_deferred_t deferred;
  memset(&deferred, 0, sizeof(deferred));

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
  while (curr_code != VM_NO_CHAR &&
         (threads.len > 0 || deferred.len > 0 || vm.poss_pending.len > 0 || !program->is_anchored) &&
         !(vm.has_match && threads.len == 0 && deferred.len == 0 && vm.poss_pending.len == 0)) {
    // Inject any deferred threads (from BACK_REF continuations) that are ready
    // at the current position. They are appended before the run-scan so that
    // the run-scan guard (`threads.len == 1`) is respected correctly.
    if (deferred.len > 0) {
      err = br_deferred_inject(&deferred, pos, &threads);
      if (err != NK_SUCCESS) {
        goto done;
      }
    }

    // ASCII run scan: when exactly one thread is at a single-character state
    // (CHAR_CLASS or CODE) and the current character matches, fast-advance
    // through the entire consecutive run to replace O(N) epsilon closures with
    // O(1). CODE scan is guarded to loop-head states only (next is epsilon).
    // Disabled for programs with back-references: the greedy scan would
    // consume quantifier runs at maximum length, preventing the NFA from
    // exploring shorter matches that are required for back-reference patterns
    // like `(\w+)\1`.
    if (threads.len == 1 && curr_code < 0x80u && !vm.has_match && !program->has_back_refs) {
      const nk_vm_state_t* scan_state = &program->states[threads.items[0].state_index];
      size_t scan = 0;

      if (scan_state->op == NK_VM_OP_CHAR_CLASS) {
        const nk_vm_char_class_t* cc = &program->char_classes[scan_state->char_class_index];
        if (cc->ascii_lookup[curr_code] != 0u) {
          // Only scan for a loop-head state: if the next state records a capture
          // boundary (CAP_BEGIN/CAP_END) or starts a back-reference, scanning
          // past multiple characters would set capture positions incorrectly.
          uint32_t cc_next_idx = scan_state->next;
          bool cc_can_scan = false;
          if (cc_next_idx < (uint32_t)program->states_len) {
            nk_vm_op_t cc_next_op = program->states[cc_next_idx].op;
            cc_can_scan =
              (cc_next_op != NK_VM_OP_CODE && cc_next_op != NK_VM_OP_CHAR_CLASS && cc_next_op != NK_VM_OP_DOT &&
               cc_next_op != NK_VM_OP_MATCH && cc_next_op != NK_VM_OP_CAP_BEGIN && cc_next_op != NK_VM_OP_CAP_END &&
               cc_next_op != NK_VM_OP_BACK_REF && cc_next_op != NK_VM_OP_POSSESSIVE);
          }
          if (cc_can_scan) {
            scan = pos + 1 + ascii_class_run_length(cc, subject_bytes + pos + 1, subject_len - pos - 1);
          }
        }
      } else if (scan_state->op == NK_VM_OP_CODE && state_matches_code(program, scan_state, curr_code)) {
        // Only scan a consecutive run when the state's successor is an epsilon
        // (non-consuming) state, indicating this is a loop-head (e.g., `a+`).
        // If the next state is a consuming op, this CODE is part of a sequence
        // like "foo" and scanning would incorrectly consume the wrong literal.
        // CAP_BEGIN/CAP_END/BACK_REF must also be excluded: scanning past
        // multiple characters would record capture positions at the wrong offset.
        uint32_t next_idx = scan_state->next;
        if (next_idx < (uint32_t)program->states_len) {
          nk_vm_op_t next_op = program->states[next_idx].op;
          if (next_op != NK_VM_OP_CODE && next_op != NK_VM_OP_CHAR_CLASS && next_op != NK_VM_OP_DOT &&
              next_op != NK_VM_OP_MATCH && next_op != NK_VM_OP_CAP_BEGIN && next_op != NK_VM_OP_CAP_END &&
              next_op != NK_VM_OP_BACK_REF && next_op != NK_VM_OP_POSSESSIVE) {
            scan = pos + 1;
            if ((scan_state->fold_flags & NK_FOLD_ASCII_ONLY) != 0 && scan_state->code < 0x80u) {
              // Fold both sides to lowercase for the inner scan loop.
              uint8_t fp = (uint8_t)scan_state->code;
              uint8_t fp_lower = (fp >= 'A' && fp <= 'Z') ? (uint8_t)(fp | 0x20u) : fp;
              while (scan < subject_len) {
                uint8_t b = subject_bytes[scan];
                uint8_t bl = (b >= 'A' && b <= 'Z') ? (uint8_t)(b | 0x20u) : b;
                if (bl != fp_lower) break;
                scan++;
              }
            } else {
              uint8_t code_byte = (uint8_t)scan_state->code;
              while (scan < subject_len && subject_bytes[scan] == code_byte) {
                scan++;
              }
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

      if (state->op == NK_VM_OP_BACK_REF) {
        // Get capture bounds (no_caps is always false here due to has_back_refs).
        size_t cap_begin = NK_REGION_POS_NONE;
        size_t cap_end = NK_REGION_POS_NONE;
        if (thread->caps != NULL && thread->caps != NO_CAPS_PTR) {
          cap_begin = thread->caps->data[2 * (size_t)state->cap_num];
          cap_end = thread->caps->data[2 * (size_t)state->cap_num + 1];
        }

        bool br_matched = false;
        size_t br_match_len = 0;
        if (cap_begin != NK_REGION_POS_NONE && cap_end != NK_REGION_POS_NONE) {
          err = back_ref_match(
            enc,
            subject_bytes,
            subject_len,
            pos,
            cap_begin,
            cap_end,
            state->is_ignore_case,
            state->fold_flags,
            &br_matched,
            &br_match_len
          );
          if (err != NK_SUCCESS) {
            goto done;
          }
        }

        if (br_matched) {
          size_t new_pos = pos + br_match_len;

          // Save vm closure state (assertions depend on prev/curr/next_code).
          size_t saved_closure_pos = vm.closure_pos;
          uint32_t saved_prev_code = vm.prev_code;
          uint32_t saved_curr_code = vm.curr_code;
          uint32_t saved_next_code = vm.next_code;

          // Set up vm for new_pos.
          vm.closure_pos = new_pos;
          vm.prev_code = decode_code_before(enc, subject_bytes, new_pos);
          uint32_t br_curr_code = VM_NO_CHAR;
          size_t br_curr_width = 0;
          err = decode_char(enc, subject_bytes + new_pos, subject_bytes_end, &br_curr_code, &br_curr_width);
          if (err != NK_SUCCESS) {
            goto done;
          }
          uint32_t br_next_code = VM_NO_CHAR;
          size_t br_next_width = 0;
          err =
            decode_char(enc, subject_bytes + new_pos + br_curr_width, subject_bytes_end, &br_next_code, &br_next_width);
          if (err != NK_SUCCESS) {
            goto done;
          }
          vm.curr_code = br_curr_code;
          vm.next_code = br_next_code;

          // Fresh dedup token: BACK_REF continuations at new_pos must not share
          // dedup namespace with other positions.
          next_token(&vm);

          uint64_t version_before = vm.match_version;
          caps_t* caps = thread->caps;
          thread->caps = NULL;

          if (new_pos == advance_pos) {
            // Common case: BACK_REF consumed exactly one character width.
            // Put continuation directly into next_threads.
            err = closure(&vm, state->next, thread->keep_pos, caps, &next_threads);
          } else {
            // Multi-char or zero-char BACK_REF: use deferred threads.
            thread_list_t br_temp;
            memset(&br_temp, 0, sizeof(br_temp));
            err = closure(&vm, state->next, thread->keep_pos, caps, &br_temp);
            if (err == NK_SUCCESS) {
              for (size_t j = 0; j < br_temp.len; j++) {
                thread_t* bt = &br_temp.items[j];
                nk_error_t push_err = br_deferred_push(&deferred, new_pos, bt->state_index, bt->keep_pos, bt->caps);
                if (push_err != NK_SUCCESS) {
                  // Unref remaining
                  for (size_t k = j; k < br_temp.len; k++) {
                    caps_unref(br_temp.items[k].caps);
                  }
                  free(br_temp.items);
                  err = push_err;
                  break;
                }
                bt->caps = NULL;
              }
            }
            free(br_temp.items);
          }

          // Restore vm closure state for subsequent threads.
          vm.closure_pos = saved_closure_pos;
          vm.prev_code = saved_prev_code;
          vm.curr_code = saved_curr_code;
          vm.next_code = saved_next_code;
          // Restore token so subsequent regular threads share this step's dedup.
          next_token(&vm);

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
      } else if (state_matches_code(program, state, curr_code)) {
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

    // Process possessive pending entries that have reached advance_pos.
    // Entries are processed in insertion order (highest priority first).
    // vm.closure_pos and the character window are already set to advance_pos.
    for (size_t pi = 0; pi < vm.poss_pending.len;) {
      poss_pending_entry_t* pe = &vm.poss_pending.items[pi];
      if (pe->target_pos == advance_pos) {
        caps_t* pc = pe->caps;
        pe->caps = NULL;
        uint32_t psi = pe->state_index;
        size_t pkp = pe->keep_pos;
        vm.poss_pending.items[pi] = vm.poss_pending.items[--vm.poss_pending.len];
        // Skip lower-priority entries: an earlier-starting match already won.
        if (vm.has_match && vm.match_keep_pos < pkp) {
          caps_unref(pc);
        } else {
          err = closure(&vm, psi, pkp, pc, &next_threads);
          if (err != NK_SUCCESS) {
            goto done;
          }
        }
      } else {
        pi++;
      }
    }

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
  br_deferred_free(&deferred);
  poss_pending_free(&vm.poss_pending);
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
