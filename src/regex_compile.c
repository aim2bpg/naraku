/**
 * @file regex_compile.c
 */

#include <naraku_regex.h>

#include <stdlib.h>
#include <string.h>

// ============================================================================
//
// Character class set (inversion list):
//
// ============================================================================

// The largest code point a compiled character class can contain. Negation is
// computed against `[0, CC_MAX_CODE]`; for encodings whose code points are
// smaller, the unreachable part of a negated class is simply never consulted
// at match time.
#define CC_MAX_CODE UINT32_C(0x10FFFF)

typedef struct {
  size_t len;  // number of inclusive [begin, end] pairs
  size_t cap;  // capacity in pairs
  uint32_t* pairs;
} cc_set_t;

static void cc_init(cc_set_t* cc) {
  cc->len = 0;
  cc->cap = 0;
  cc->pairs = NULL;
}

static void cc_free(cc_set_t* cc) {
  free(cc->pairs);
  cc_init(cc);
}

static nk_error_t cc_reserve(cc_set_t* cc, size_t pairs_len) {
  if (pairs_len <= cc->cap) {
    return NK_SUCCESS;
  }

  size_t new_cap = cc->cap == 0 ? 8 : cc->cap * 2;
  while (new_cap < pairs_len) {
    new_cap *= 2;
  }

  uint32_t* new_pairs = (uint32_t*)realloc(cc->pairs, new_cap * 2 * sizeof(uint32_t));
  if (new_pairs == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  cc->pairs = new_pairs;
  cc->cap = new_cap;
  return NK_SUCCESS;
}

// Adds the inclusive range `[lo, hi]`, merging overlapping and adjacent pairs
// so that the list stays sorted and non-overlapping.
static nk_error_t cc_add_range(cc_set_t* cc, uint32_t lo, uint32_t hi) {
  if (lo > hi) {
    return NK_SUCCESS;
  }

  uint32_t merge_lo = lo == 0 ? 0 : lo - 1;
  uint32_t merge_hi = hi == UINT32_MAX ? hi : hi + 1;

  size_t left = 0;
  while (left < cc->len && cc->pairs[2 * left + 1] < merge_lo) {
    left++;
  }
  size_t right = left;
  while (right < cc->len && cc->pairs[2 * right] <= merge_hi) {
    right++;
  }

  uint32_t new_lo = lo;
  uint32_t new_hi = hi;
  if (left < right) {
    if (cc->pairs[2 * left] < new_lo) {
      new_lo = cc->pairs[2 * left];
    }
    if (cc->pairs[2 * (right - 1) + 1] > new_hi) {
      new_hi = cc->pairs[2 * (right - 1) + 1];
    }
  }

  if (left == right) {
    nk_error_t err = cc_reserve(cc, cc->len + 1);
    if (err != NK_SUCCESS) {
      return err;
    }
    memmove(&cc->pairs[2 * (left + 1)], &cc->pairs[2 * left], (cc->len - left) * 2 * sizeof(uint32_t));
    cc->pairs[2 * left] = new_lo;
    cc->pairs[2 * left + 1] = new_hi;
    cc->len++;
    return NK_SUCCESS;
  }

  cc->pairs[2 * left] = new_lo;
  cc->pairs[2 * left + 1] = new_hi;
  size_t removed = right - left - 1;
  if (removed > 0) {
    memmove(&cc->pairs[2 * (left + 1)], &cc->pairs[2 * right], (cc->len - right) * 2 * sizeof(uint32_t));
    cc->len -= removed;
  }
  return NK_SUCCESS;
}

static nk_error_t cc_add_set(cc_set_t* cc, const cc_set_t* other) {
  for (size_t i = 0; i < other->len; i++) {
    nk_error_t err = cc_add_range(cc, other->pairs[2 * i], other->pairs[2 * i + 1]);
    if (err != NK_SUCCESS) {
      return err;
    }
  }
  return NK_SUCCESS;
}

// Replaces `cc` with its intersection with `other`.
static nk_error_t cc_intersect(cc_set_t* cc, const cc_set_t* other) {
  cc_set_t out;
  cc_init(&out);

  size_t i = 0;
  size_t j = 0;
  while (i < cc->len && j < other->len) {
    uint32_t a_lo = cc->pairs[2 * i];
    uint32_t a_hi = cc->pairs[2 * i + 1];
    uint32_t b_lo = other->pairs[2 * j];
    uint32_t b_hi = other->pairs[2 * j + 1];
    uint32_t lo = a_lo > b_lo ? a_lo : b_lo;
    uint32_t hi = a_hi < b_hi ? a_hi : b_hi;
    if (lo <= hi) {
      nk_error_t err = cc_add_range(&out, lo, hi);
      if (err != NK_SUCCESS) {
        cc_free(&out);
        return err;
      }
    }
    if (a_hi < b_hi) {
      i++;
    } else {
      j++;
    }
  }

  cc_free(cc);
  *cc = out;
  return NK_SUCCESS;
}

// Replaces `cc` with its complement within `[0, CC_MAX_CODE]`.
static nk_error_t cc_negate(cc_set_t* cc) {
  cc_set_t out;
  cc_init(&out);

  uint32_t next = 0;
  bool exhausted = false;
  for (size_t i = 0; i < cc->len && !exhausted; i++) {
    uint32_t lo = cc->pairs[2 * i];
    uint32_t hi = cc->pairs[2 * i + 1];
    if (lo > CC_MAX_CODE) {
      break;
    }
    if (lo > next) {
      nk_error_t err = cc_add_range(&out, next, lo - 1);
      if (err != NK_SUCCESS) {
        cc_free(&out);
        return err;
      }
    }
    if (hi >= CC_MAX_CODE) {
      exhausted = true;
    } else {
      next = hi + 1;
    }
  }
  if (!exhausted) {
    nk_error_t err = cc_add_range(&out, next, CC_MAX_CODE);
    if (err != NK_SUCCESS) {
      cc_free(&out);
      return err;
    }
  }

  cc_free(cc);
  *cc = out;
  return NK_SUCCESS;
}

// Returns true if `code` is contained in `cc` (binary search on pairs).
static bool cc_contains(const cc_set_t* cc, uint32_t code) {
  size_t lo = 0, hi = cc->len;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (code < cc->pairs[2 * mid]) {
      hi = mid;
    } else if (code > cc->pairs[2 * mid + 1]) {
      lo = mid + 1;
    } else {
      return true;
    }
  }
  return false;
}

// Callback context for cc_simple_fold_expand.
// Collects unique multi-char (fold_len > 1) fold sequences for code points
// that are present in `cc`. Used by compile_char_class to generate SPLIT
// alternatives so that e.g. [ß]/i also matches "ss".
typedef struct {
  const cc_set_t* cc;
  uint32_t folds[32][NK_ENC_MAX_FOLDED_CODES];
  size_t fold_lens[32];
  size_t count;
} cc_multi_fold_ctx_t;

static nk_error_t cc_multi_fold_cb(uint32_t unfolded, const uint32_t* folded, size_t fold_len, void* data) {
  cc_multi_fold_ctx_t* ctx = (cc_multi_fold_ctx_t*)data;
  if (fold_len <= 1 || ctx->count >= 32) return NK_SUCCESS;
  if (!cc_contains(ctx->cc, unfolded)) return NK_SUCCESS;
  for (size_t i = 0; i < ctx->count; i++) {
    if (ctx->fold_lens[i] == fold_len && memcmp(ctx->folds[i], folded, fold_len * sizeof(uint32_t)) == 0) {
      return NK_SUCCESS;
    }
  }
  memcpy(ctx->folds[ctx->count], folded, fold_len * sizeof(uint32_t));
  ctx->fold_lens[ctx->count] = fold_len;
  ctx->count++;
  return NK_SUCCESS;
}

typedef struct {
  const cc_set_t* original;
  cc_set_t additions;
  nk_error_t err;
} cc_fold_ctx_t;

static nk_error_t cc_fold_expand_cb(uint32_t unfolded, const uint32_t* folded, size_t fold_len, void* data) {
  cc_fold_ctx_t* ctx = (cc_fold_ctx_t*)data;
  if (ctx->err != NK_SUCCESS) return NK_SUCCESS;
  if (fold_len != 1) return NK_SUCCESS;  // simple 1-to-1 fold only
  uint32_t f = folded[0];
  if (cc_contains(ctx->original, unfolded) && !cc_contains(ctx->original, f)) {
    ctx->err = cc_add_range(&ctx->additions, f, f);
  }
  if (cc_contains(ctx->original, f) && !cc_contains(ctx->original, unfolded)) {
    ctx->err = cc_add_range(&ctx->additions, unfolded, unfolded);
  }
  return NK_SUCCESS;
}

// Expands simple (1-to-1) Unicode case folding into `cc` by scanning the full
// fold table and adding the case partner for every code point already in `cc`.
static nk_error_t cc_simple_fold_expand(const nk_encoding_t* enc, nk_fold_flag_t flags, cc_set_t* cc) {
  cc_fold_ctx_t ctx;
  cc_init(&ctx.additions);
  ctx.original = cc;
  ctx.err = NK_SUCCESS;
  nk_error_t err = nk_enc_iterate_case_fold(enc, flags, cc_fold_expand_cb, &ctx);
  if (err == NK_SUCCESS) err = ctx.err;
  if (err == NK_SUCCESS) err = cc_add_set(cc, &ctx.additions);
  cc_free(&ctx.additions);
  return err;
}

// Expands ASCII case folding into `cc`: for every letter in A-Z / a-z that is
// already in `cc`, adds its fold counterpart so the class matches both cases.
static nk_error_t cc_ascii_fold_expand(cc_set_t* cc) {
  for (uint32_t c = 'A'; c <= 'Z'; c++) {
    uint32_t lower = c | 0x20u;
    bool has_upper = cc_contains(cc, c);
    bool has_lower = cc_contains(cc, lower);
    if (has_upper && !has_lower) {
      nk_error_t err = cc_add_range(cc, lower, lower);
      if (err != NK_SUCCESS) return err;
    }
    if (has_lower && !has_upper) {
      nk_error_t err = cc_add_range(cc, c, c);
      if (err != NK_SUCCESS) return err;
    }
  }
  return NK_SUCCESS;
}

// ============================================================================
//
// Compiler context, state emission, and hole patching:
//
// ============================================================================

// An unfilled transition of an already emitted state: the `next` (or
// `split_next`) field that will be patched once the following fragment's
// entry state is known.
typedef struct {
  uint32_t state_index;
  bool is_split_next;
} hole_t;

typedef struct {
  size_t len;
  size_t cap;
  hole_t* items;
} hole_list_t;

static void hole_list_init(hole_list_t* holes) {
  holes->len = 0;
  holes->cap = 0;
  holes->items = NULL;
}

static void hole_list_free(hole_list_t* holes) {
  free(holes->items);
  hole_list_init(holes);
}

static nk_error_t hole_list_push(hole_list_t* holes, uint32_t state_index, bool is_split_next) {
  if (holes->len == holes->cap) {
    size_t new_cap = holes->cap == 0 ? 8 : holes->cap * 2;
    hole_t* new_items = (hole_t*)realloc(holes->items, new_cap * sizeof(hole_t));
    if (new_items == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    holes->items = new_items;
    holes->cap = new_cap;
  }
  holes->items[holes->len].state_index = state_index;
  holes->items[holes->len].is_split_next = is_split_next;
  holes->len++;
  return NK_SUCCESS;
}

// Appends all holes of `src` to `dst` and empties `src` (keeping its storage).
static nk_error_t hole_list_move_append(hole_list_t* dst, hole_list_t* src) {
  for (size_t i = 0; i < src->len; i++) {
    nk_error_t err = hole_list_push(dst, src->items[i].state_index, src->items[i].is_split_next);
    if (err != NK_SUCCESS) {
      return err;
    }
  }
  src->len = 0;
  return NK_SUCCESS;
}

typedef struct {
  const nk_encoding_t* enc;
  nk_program_t* program;
  size_t states_cap;
  size_t char_classes_cap;
  uint32_t next_check_id;
  uint32_t next_epsilon_check_id;

  // Span of the innermost AST node that caused a compile error:
  size_t error_offset;
  size_t error_length;
  bool has_error_span;

  // Capture name map entries (for resolving back-references):
  const nk_capture_entry_t* capture_entries;
  size_t capture_entries_len;
  // Total capture group count from the parser (passed to inner sub-program compiles):
  uint32_t num_capture_groups;
} compiler_t;

static nk_error_t emit(compiler_t* c, nk_vm_op_t op, uint32_t* out_index) {
  nk_program_t* program = c->program;

  if (program->states_len >= NK_MAX_VM_STATES) {
    return NK_ERR_PATTERN_TOO_COMPLEX;
  }
  if (program->states_len == c->states_cap) {
    size_t new_cap = c->states_cap == 0 ? 64 : c->states_cap * 2;
    nk_vm_state_t* new_states = (nk_vm_state_t*)realloc(program->states, new_cap * sizeof(nk_vm_state_t));
    if (new_states == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    program->states = new_states;
    c->states_cap = new_cap;
  }

  nk_vm_state_t* state = &program->states[program->states_len];
  state->op = op;
  state->code = 0;
  state->char_class_index = 0;
  state->allows_newline = false;
  state->assertion_type = NK_ASSERTION_TYPE_BEGIN_OF_LINE;
  state->cap_num = 0;
  state->check_id = 0;
  state->next = NK_VM_STATE_NONE;
  state->split_next = NK_VM_STATE_NONE;
  state->fold_flags = NK_FOLD_DEFAULT;
  state->is_ignore_case = false;
  state->lookaround_prog_idx = 0;
  state->lookaround_is_positive = false;
  state->lookaround_is_ahead = false;
  state->possessive_prog_idx = 0;

  switch (op) {
    case NK_VM_OP_CODE:
    case NK_VM_OP_CHAR_CLASS:
    case NK_VM_OP_DOT:
    case NK_VM_OP_MATCH:
    case NK_VM_OP_CHECK_VISITED:
      state->check_id = c->next_check_id++;
      break;
    default:
      break;
  }

  *out_index = (uint32_t)program->states_len;
  program->states_len++;
  return NK_SUCCESS;
}

static void patch_hole(compiler_t* c, hole_t hole, uint32_t target) {
  nk_vm_state_t* state = &c->program->states[hole.state_index];
  if (hole.is_split_next) {
    state->split_next = target;
  } else {
    state->next = target;
  }
}

// Patches every hole to `target`. When two or more epsilon paths converge, a
// `CHECK_VISITED` state is inserted in front of `target` so the VM walks the
// merged suffix only once per closure.
static nk_error_t patch_all(compiler_t* c, hole_list_t* holes, uint32_t target) {
  uint32_t patched_target = target;
  if (holes->len >= 2) {
    uint32_t check_index;
    nk_error_t err = emit(c, NK_VM_OP_CHECK_VISITED, &check_index);
    if (err != NK_SUCCESS) {
      return err;
    }
    c->program->states[check_index].next = target;
    patched_target = check_index;
  }
  for (size_t i = 0; i < holes->len; i++) {
    patch_hole(c, holes->items[i], patched_target);
  }
  holes->len = 0;
  return NK_SUCCESS;
}

// ============================================================================
//
// Character class construction from the AST:
//
// ============================================================================

static nk_cprop_t char_type_to_cprop(nk_char_type_t char_type) {
  switch (char_type) {
    case NK_CHAR_TYPE_WORD:
      return NK_CPROP_WORD;
    case NK_CHAR_TYPE_DIGIT:
      return NK_CPROP_DIGIT;
    case NK_CHAR_TYPE_SPACE:
      return NK_CPROP_SPACE;
    case NK_CHAR_TYPE_HEX_DIGIT:
      return NK_CPROP_XDIGIT;
  }
  return NK_CPROP_WORD;
}

static nk_cprop_t posix_char_class_to_cprop(nk_posix_char_class_t posix_char_class) {
  switch (posix_char_class) {
    case NK_POSIX_CHAR_CLASS_ALNUM:
      return NK_CPROP_ALNUM;
    case NK_POSIX_CHAR_CLASS_ALPHA:
      return NK_CPROP_ALPHA;
    case NK_POSIX_CHAR_CLASS_BLANK:
      return NK_CPROP_BLANK;
    case NK_POSIX_CHAR_CLASS_CNTRL:
      return NK_CPROP_CNTRL;
    case NK_POSIX_CHAR_CLASS_DIGIT:
      return NK_CPROP_DIGIT;
    case NK_POSIX_CHAR_CLASS_GRAPH:
      return NK_CPROP_GRAPH;
    case NK_POSIX_CHAR_CLASS_LOWER:
      return NK_CPROP_LOWER;
    case NK_POSIX_CHAR_CLASS_PRINT:
      return NK_CPROP_PRINT;
    case NK_POSIX_CHAR_CLASS_PUNCT:
      return NK_CPROP_PUNCT;
    case NK_POSIX_CHAR_CLASS_SPACE:
      return NK_CPROP_SPACE;
    case NK_POSIX_CHAR_CLASS_UPPER:
      return NK_CPROP_UPPER;
    case NK_POSIX_CHAR_CLASS_XDIGIT:
      return NK_CPROP_XDIGIT;
    case NK_POSIX_CHAR_CLASS_ASCII:
      return NK_CPROP_ASCII;
    case NK_POSIX_CHAR_CLASS_WORD:
      return NK_CPROP_WORD;
  }
  return NK_CPROP_WORD;
}

// Builds the code point set for a character property, handling the encoding's
// delegation strategy, then applies the ASCII-only restriction and negation.
static nk_error_t
cc_from_cprop(compiler_t* c, nk_cprop_t cprop, bool is_ascii_only, bool is_positive, cc_set_t* out_cc) {
  cc_init(out_cc);

  nk_code_range_delegation_t delegation;
  nk_static_code_range_t code_range = {0, NULL};
  nk_error_t err = nk_enc_get_cprop_code_range(c->enc, cprop, &delegation, &code_range);
  if (err != NK_SUCCESS) {
    return err;
  }

  switch (delegation) {
    case NK_ENC_NO_DELEGATION:
      for (size_t i = 0; i < code_range.len; i++) {
        err = cc_add_range(out_cc, code_range.intervals[2 * i], code_range.intervals[2 * i + 1]);
        if (err != NK_SUCCESS) {
          cc_free(out_cc);
          return err;
        }
      }
      break;
    case NK_ENC_7BIT_DELEGATE:
    case NK_ENC_8BIT_DELEGATE:
    {
      uint32_t limit = delegation == NK_ENC_7BIT_DELEGATE ? 0x7F : 0xFF;
      for (uint32_t code = 0; code <= limit; code++) {
        if (nk_enc_code_is_cprop(c->enc, code, cprop)) {
          err = cc_add_range(out_cc, code, code);
          if (err != NK_SUCCESS) {
            cc_free(out_cc);
            return err;
          }
        }
      }
      break;
    }
  }

  if (is_ascii_only) {
    cc_set_t ascii;
    cc_init(&ascii);
    err = cc_add_range(&ascii, 0, 0x7F);
    if (err == NK_SUCCESS) {
      err = cc_intersect(out_cc, &ascii);
    }
    cc_free(&ascii);
    if (err != NK_SUCCESS) {
      cc_free(out_cc);
      return err;
    }
  }

  if (!is_positive) {
    err = cc_negate(out_cc);
    if (err != NK_SUCCESS) {
      cc_free(out_cc);
      return err;
    }
  }

  return NK_SUCCESS;
}

static nk_error_t cc_add_item(compiler_t* c, const nk_char_class_item_t* item, cc_set_t* cc);

// Builds the set for a character class body: items within a union are added
// together, the unions are intersected (`&&`), and `is_positive == false`
// negates the result.
static nk_error_t
cc_from_unions(compiler_t* c, nk_char_class_union_t** unions, size_t unions_len, bool is_positive, cc_set_t* out_cc) {
  cc_init(out_cc);

  for (size_t i = 0; i < unions_len; i++) {
    cc_set_t union_cc;
    cc_init(&union_cc);
    nk_error_t err = NK_SUCCESS;
    for (size_t j = 0; j < unions[i]->items_len && err == NK_SUCCESS; j++) {
      err = cc_add_item(c, unions[i]->items[j], &union_cc);
    }
    if (err == NK_SUCCESS) {
      if (i == 0) {
        *out_cc = union_cc;  // move; no intersection for the first union
        continue;
      }
      err = cc_intersect(out_cc, &union_cc);
    }
    cc_free(&union_cc);
    if (err != NK_SUCCESS) {
      cc_free(out_cc);
      return err;
    }
  }

  if (!is_positive) {
    nk_error_t err = cc_negate(out_cc);
    if (err != NK_SUCCESS) {
      cc_free(out_cc);
      return err;
    }
  }

  return NK_SUCCESS;
}

static nk_error_t cc_add_item(compiler_t* c, const nk_char_class_item_t* item, cc_set_t* cc) {
  switch (item->type) {
    case NK_CHAR_CLASS_ITEM_TYPE_CODE:
      return cc_add_range(cc, item->data.code, item->data.code);
    case NK_CHAR_CLASS_ITEM_TYPE_RANGE:
      return cc_add_range(cc, item->data.range.begin_code, item->data.range.end_code);
    case NK_CHAR_CLASS_ITEM_TYPE_CHAR_TYPE:
    {
      cc_set_t sub;
      nk_error_t err = cc_from_cprop(
        c,
        char_type_to_cprop(item->data.char_type.char_type),
        item->data.char_type.is_ascii_only,
        item->data.char_type.is_positive,
        &sub
      );
      if (err == NK_SUCCESS) {
        err = cc_add_set(cc, &sub);
        cc_free(&sub);
      }
      return err;
    }
    case NK_CHAR_CLASS_ITEM_TYPE_POSIX_CHAR_CLASS:
    {
      cc_set_t sub;
      nk_error_t err = cc_from_cprop(
        c,
        posix_char_class_to_cprop(item->data.posix_char_class.posix_char_class),
        item->data.posix_char_class.is_ascii_only,
        item->data.posix_char_class.is_positive,
        &sub
      );
      if (err == NK_SUCCESS) {
        err = cc_add_set(cc, &sub);
        cc_free(&sub);
      }
      return err;
    }
    case NK_CHAR_CLASS_ITEM_TYPE_CHAR_PROP:
    {
      cc_set_t sub;
      nk_error_t err = cc_from_cprop(c, item->data.char_prop.cprop, false, item->data.char_prop.is_positive, &sub);
      if (err == NK_SUCCESS) {
        err = cc_add_set(cc, &sub);
        cc_free(&sub);
      }
      return err;
    }
    case NK_CHAR_CLASS_ITEM_TYPE_NESTED_CHAR_CLASS:
    {
      cc_set_t sub;
      nk_error_t err = cc_from_unions(
        c,
        item->data.nested_char_class.unions,
        item->data.nested_char_class.unions_len,
        item->data.nested_char_class.is_positive,
        &sub
      );
      if (err == NK_SUCCESS) {
        err = cc_add_set(cc, &sub);
        cc_free(&sub);
      }
      return err;
    }
  }
  return NK_ERR_INTERNAL_ERROR;
}

// Moves `cc` into the program's character class table; `cc` is released.
static nk_error_t program_add_char_class(compiler_t* c, cc_set_t* cc, uint32_t* out_index) {
  nk_program_t* program = c->program;

  if (program->char_classes_len == c->char_classes_cap) {
    size_t new_cap = c->char_classes_cap == 0 ? 8 : c->char_classes_cap * 2;
    nk_vm_char_class_t* new_classes =
      (nk_vm_char_class_t*)realloc(program->char_classes, new_cap * sizeof(nk_vm_char_class_t));
    if (new_classes == NULL) {
      cc_free(cc);
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    program->char_classes = new_classes;
    c->char_classes_cap = new_cap;
  }

  nk_vm_char_class_t* vm_cc = &program->char_classes[program->char_classes_len];
  vm_cc->ranges_len = cc->len;
  vm_cc->ranges = NULL;
  if (cc->len > 0) {
    vm_cc->ranges = (uint32_t*)malloc(cc->len * 2 * sizeof(uint32_t));
    if (vm_cc->ranges == NULL) {
      cc_free(cc);
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    memcpy(vm_cc->ranges, cc->pairs, cc->len * 2 * sizeof(uint32_t));
  }

  vm_cc->ascii_bits[0] = 0;
  vm_cc->ascii_bits[1] = 0;
  memset(vm_cc->ascii_lookup, 0, sizeof(vm_cc->ascii_lookup));
  for (size_t i = 0; i < cc->len; i++) {
    uint32_t lo = cc->pairs[2 * i];
    uint32_t hi = cc->pairs[2 * i + 1];
    if (lo > 0x7F) {
      break;
    }
    if (hi > 0x7F) {
      hi = 0x7F;
    }
    for (uint32_t code = lo; code <= hi; code++) {
      vm_cc->ascii_bits[code >> 6] |= (uint64_t)1 << (code & 0x3F);
      vm_cc->ascii_lookup[code] = 1u;
    }
  }

  *out_index = (uint32_t)program->char_classes_len;
  program->char_classes_len++;
  cc_free(cc);
  return NK_SUCCESS;
}

// ============================================================================
//
// AST compilation:
//
// ============================================================================

static bool node_can_match_empty(const nk_node_t* node) {
  if (node == NULL) {
    return true;
  }

  switch (node->base.type) {
    case NK_NODE_TYPE_LITERAL:
      return node->literal.buf.bytes == node->literal.buf.bytes_end;
    case NK_NODE_TYPE_CHAR_CLASS:
    case NK_NODE_TYPE_CHAR_TYPE:
    case NK_NODE_TYPE_CHAR_PROP:
    case NK_NODE_TYPE_DOT:
    case NK_NODE_TYPE_NEWLINE:
    case NK_NODE_TYPE_GRAPHEME_CLUSTER:
      return false;
    case NK_NODE_TYPE_KEEP:
    case NK_NODE_TYPE_ASSERTION:
      return true;
    case NK_NODE_TYPE_QUANTIFIER:
      return node->quantifier.min == 0 || node_can_match_empty(node->quantifier.child);
    case NK_NODE_TYPE_CAPTURE:
      return node_can_match_empty(node->capture.child);
    case NK_NODE_TYPE_GROUP:
      return node_can_match_empty(node->group.child);
    case NK_NODE_TYPE_CONCAT:
      for (size_t i = 0; i < node->concat.children_len; i++) {
        if (!node_can_match_empty(node->concat.children[i])) {
          return false;
        }
      }
      return true;
    case NK_NODE_TYPE_ALT:
      for (size_t i = 0; i < node->alt.children_len; i++) {
        if (node_can_match_empty(node->alt.children[i])) {
          return true;
        }
      }
      return false;
    // These are rejected by the compiler before the answer matters; be
    // conservative so a surrounding loop still gets the epsilon guard.
    case NK_NODE_TYPE_UNKNOWN:
    case NK_NODE_TYPE_BACK_REF:
    case NK_NODE_TYPE_CALL:
    case NK_NODE_TYPE_ATOMIC:
    case NK_NODE_TYPE_ABSENCE:
    case NK_NODE_TYPE_CONDITIONAL:
      return true;
  }

  return true;
}

// Compiles `node` into a fragment: writes its entry state to `*out_initial`
// and appends its dangling exits to `out_holes` (which the caller initializes
// and owns).
static nk_error_t compile_node(compiler_t* c, const nk_node_t* node, uint32_t* out_initial, hole_list_t* out_holes);

// Compiles an empty fragment: a single JUMP whose target is left as a hole.
static nk_error_t compile_epsilon(compiler_t* c, uint32_t* out_initial, hole_list_t* out_holes) {
  uint32_t jump_index;
  nk_error_t err = emit(c, NK_VM_OP_JUMP, &jump_index);
  if (err != NK_SUCCESS) {
    return err;
  }
  *out_initial = jump_index;
  return hole_list_push(out_holes, jump_index, false);
}

// Registers `cc` (consuming it) and emits a CHAR_CLASS state matching it.
static nk_error_t compile_cc_state(compiler_t* c, cc_set_t* cc, uint32_t* out_initial, hole_list_t* out_holes) {
  uint32_t cc_index;
  nk_error_t err = program_add_char_class(c, cc, &cc_index);
  if (err != NK_SUCCESS) {
    return err;
  }

  uint32_t state_index;
  err = emit(c, NK_VM_OP_CHAR_CLASS, &state_index);
  if (err != NK_SUCCESS) {
    return err;
  }
  c->program->states[state_index].char_class_index = cc_index;

  *out_initial = state_index;
  return hole_list_push(out_holes, state_index, false);
}

// Builds an alternation NFA matching any code point (or short sequence) whose
// case fold equals `folded_codes[0..folded_len-1]`.
//
// The identity (the folded code itself) is always the first alternative, and
// nk_enc_expand_case_unfold() supplies any additional code points. Continuations
// are compiled once per unique consumed count and shared across alternatives,
// keeping NFA size linear in `folded_len` rather than exponential.
//
// Example: folded_codes=[s,s] (from pattern "ß" with NK_FOLD_FULL)
//   identity {s,len=1} + expand → {S,len=1}, {ß,len=2}
//   Compiled as: SPLIT(CODE(s)→cont[s] | SPLIT(CODE(S)→cont[s] | CODE(ß)))
//   where cont[s] = SPLIT(CODE(s) | CODE(S))
static nk_error_t compile_full_fold_seq(
  compiler_t* c,
  const nk_encoding_t* enc,
  nk_fold_flag_t flags,
  const uint32_t* folded_codes,
  size_t folded_len,
  uint32_t* out_initial,
  hole_list_t* out_holes
) {
  if (folded_len == 0) {
    return compile_epsilon(c, out_initial, out_holes);
  }

  // Build alternatives: identity first, then whatever expand_case_unfold finds.
  nk_unfold_item_t items[NK_ENC_MAX_UNFOLD_ITEMS + 1];
  items[0].unfolded_code = folded_codes[0];
  items[0].folded_codes_len = 1;
  size_t prefix_len = folded_len < NK_ENC_MAX_FOLDED_CODES ? folded_len : NK_ENC_MAX_FOLDED_CODES;
  size_t items_count = 1 + nk_enc_expand_case_unfold(enc, flags, folded_codes, prefix_len, items + 1);

  // Compile one continuation per unique consumed count and store its entry state.
  // Indexed by consumed count (1..NK_ENC_MAX_FOLDED_CODES); index 0 is unused.
  uint32_t cont_initial_by_n[NK_ENC_MAX_FOLDED_CODES + 1];
  bool cont_done_by_n[NK_ENC_MAX_FOLDED_CODES + 1];
  for (size_t j = 0; j <= NK_ENC_MAX_FOLDED_CODES; j++) {
    cont_initial_by_n[j] = NK_VM_STATE_NONE;
    cont_done_by_n[j] = false;
  }
  for (size_t i = 0; i < items_count; i++) {
    size_t consumed = items[i].folded_codes_len;
    if (!cont_done_by_n[consumed]) {
      hole_list_t cont_holes;
      hole_list_init(&cont_holes);
      nk_error_t err = compile_full_fold_seq(
        c,
        enc,
        flags,
        folded_codes + consumed,
        folded_len - consumed,
        &cont_initial_by_n[consumed],
        &cont_holes
      );
      if (err != NK_SUCCESS) {
        hole_list_free(&cont_holes);
        return err;
      }
      cont_done_by_n[consumed] = true;
      err = hole_list_move_append(out_holes, &cont_holes);
      hole_list_free(&cont_holes);
      if (err != NK_SUCCESS) {
        return err;
      }
    }
  }

  // Emit a SPLIT chain + CODE state for each alternative.
  // CODE.next is filled from the pre-compiled continuation table above.
  uint32_t initial = NK_VM_STATE_NONE;
  uint32_t prev_split_idx = NK_VM_STATE_NONE;

  for (size_t i = 0; i < items_count; i++) {
    bool is_last = (i == items_count - 1);
    uint32_t split_idx = NK_VM_STATE_NONE;

    if (!is_last) {
      nk_error_t err = emit(c, NK_VM_OP_SPLIT, &split_idx);
      if (err != NK_SUCCESS) {
        return err;
      }
      if (initial == NK_VM_STATE_NONE) {
        initial = split_idx;
      } else {
        c->program->states[prev_split_idx].split_next = split_idx;
      }
      prev_split_idx = split_idx;
    }

    uint32_t code_idx;
    nk_error_t err = emit(c, NK_VM_OP_CODE, &code_idx);
    if (err != NK_SUCCESS) {
      return err;
    }
    c->program->states[code_idx].code = items[i].unfolded_code;
    c->program->states[code_idx].next = cont_initial_by_n[items[i].folded_codes_len];

    if (split_idx != NK_VM_STATE_NONE) {
      c->program->states[split_idx].next = code_idx;
    } else if (prev_split_idx != NK_VM_STATE_NONE) {
      c->program->states[prev_split_idx].split_next = code_idx;
    } else {
      initial = code_idx;
    }
  }

  *out_initial = initial;
  return NK_SUCCESS;
}

static nk_error_t compile_literal(compiler_t* c, const nk_node_t* node, uint32_t* out_initial, hole_list_t* out_holes) {
  const nk_literal_node_t* literal = &node->literal;

  // Full fold and Turkish/Azeri: a single pattern code point may fold to
  // multiple code points (e.g. ß→ss), so we expand the literal into an
  // alternation NFA instead of emitting plain CODE states with is_ignore_case.
  if (literal->is_ignore_case &&
      ((literal->fold_flags & NK_FOLD_FULL) != 0 || (literal->fold_flags & NK_FOLD_TURKISH_AZERI) != 0)) {
    const uint8_t* bytes = literal->buf.bytes;
    const uint8_t* bytes_end = literal->buf.bytes_end;
    if (bytes == bytes_end) {
      return compile_epsilon(c, out_initial, out_holes);
    }
    size_t byte_count = (size_t)(bytes_end - bytes);
    uint32_t* folded_codes = (uint32_t*)malloc(byte_count * NK_ENC_MAX_FOLDED_CODES * sizeof(uint32_t));
    if (folded_codes == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    size_t folded_len = 0;
    while (bytes < bytes_end) {
      int8_t width = nk_enc_scan_mbc_width(c->enc, bytes, bytes_end);
      if (width <= 0) {
        free(folded_codes);
        return NK_ERR_INTERNAL_ERROR;
      }
      uint32_t code = nk_enc_decode_mbc(c->enc, bytes, bytes_end);
      uint32_t fold_buf[NK_ENC_MAX_FOLDED_CODES];
      size_t fold_n = nk_enc_get_case_fold(c->enc, literal->fold_flags, code, fold_buf);
      for (size_t j = 0; j < fold_n; j++) {
        folded_codes[folded_len++] = fold_buf[j];
      }
      bytes += (size_t)width;
    }
    nk_error_t err =
      compile_full_fold_seq(c, c->enc, literal->fold_flags, folded_codes, folded_len, out_initial, out_holes);
    free(folded_codes);
    return err;
  }

  const uint8_t* bytes = literal->buf.bytes;
  const uint8_t* bytes_end = literal->buf.bytes_end;
  if (bytes == bytes_end) {
    return compile_epsilon(c, out_initial, out_holes);
  }

  uint32_t initial = NK_VM_STATE_NONE;
  uint32_t prev_index = NK_VM_STATE_NONE;
  while (bytes < bytes_end) {
    int8_t width = nk_enc_scan_mbc_width(c->enc, bytes, bytes_end);
    if (width <= 0) {
      // The parser already validated the pattern bytes.
      return NK_ERR_INTERNAL_ERROR;
    }
    uint32_t code = nk_enc_decode_mbc(c->enc, bytes, bytes_end);

    uint32_t state_index;
    nk_error_t err = emit(c, NK_VM_OP_CODE, &state_index);
    if (err != NK_SUCCESS) {
      return err;
    }
    c->program->states[state_index].code = code;
    if (literal->is_ignore_case) {
      c->program->states[state_index].is_ignore_case = true;
      c->program->states[state_index].fold_flags = literal->fold_flags;
    }

    if (initial == NK_VM_STATE_NONE) {
      initial = state_index;
    } else {
      c->program->states[prev_index].next = state_index;
    }
    prev_index = state_index;
    bytes += width;
  }

  *out_initial = initial;
  return hole_list_push(out_holes, prev_index, false);
}

static nk_error_t
compile_char_class(compiler_t* c, const nk_node_t* node, uint32_t* out_initial, hole_list_t* out_holes) {
  const nk_char_class_node_t* char_class = &node->char_class;

  cc_set_t cc;
  nk_error_t err = cc_from_unions(c, char_class->unions, char_class->unions_len, char_class->is_positive, &cc);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (char_class->is_ignore_case) {
    if ((char_class->fold_flags & NK_FOLD_ASCII_ONLY) != 0) {
      err = cc_ascii_fold_expand(&cc);
    } else {
      err = cc_simple_fold_expand(c->enc, char_class->fold_flags, &cc);
    }
    if (err != NK_SUCCESS) {
      cc_free(&cc);
      return err;
    }
  }

  // When full fold is active, check for multi-char fold sequences (e.g. ß→ss).
  // These cannot be added to the char class set (single code points only),
  // so compile each unique fold sequence and wire them via SPLIT alternatives.
  if (char_class->is_ignore_case && (char_class->fold_flags & NK_FOLD_ASCII_ONLY) == 0 &&
      (char_class->fold_flags & (NK_FOLD_FULL | NK_FOLD_TURKISH_AZERI)) != 0) {
    cc_multi_fold_ctx_t mf = {.cc = &cc};
    err = nk_enc_iterate_case_fold(c->enc, char_class->fold_flags, cc_multi_fold_cb, &mf);
    if (err != NK_SUCCESS) {
      cc_free(&cc);
      return err;
    }

    if (mf.count > 0) {
      // Step 1: compile all alternatives.
      // compile_cc_state -> program_add_char_class always frees cc internally.
      uint32_t cc_init;
      hole_list_t cc_holes;
      hole_list_init(&cc_holes);
      err = compile_cc_state(c, &cc, &cc_init, &cc_holes);
      if (err != NK_SUCCESS) {
        hole_list_free(&cc_holes);
        return err;
      }

      uint32_t seq_inits[32];
      hole_list_t seq_holes[32];
      size_t compiled = 0;
      for (size_t i = 0; i < mf.count; i++) {
        hole_list_init(&seq_holes[i]);
        err = compile_full_fold_seq(
          c,
          c->enc,
          char_class->fold_flags,
          mf.folds[i],
          mf.fold_lens[i],
          &seq_inits[i],
          &seq_holes[i]
        );
        if (err != NK_SUCCESS) {
          hole_list_free(&cc_holes);
          for (size_t j = 0; j < compiled; j++) hole_list_free(&seq_holes[j]);
          hole_list_free(&seq_holes[i]);
          return err;
        }
        compiled++;
      }

      // Step 2: emit SPLIT chain connecting all alternatives.
      // Alternatives are compiled first so their state indices are stable;
      // SPLIT states pointing backward into already-emitted states is valid.
      size_t n = 1 + mf.count;
      uint32_t first_split = NK_VM_STATE_NONE;
      uint32_t prev_split = NK_VM_STATE_NONE;
      for (size_t i = 0; i < n; i++) {
        uint32_t alt_init = (i == 0) ? cc_init : seq_inits[i - 1];
        bool is_last = (i == n - 1);
        if (!is_last) {
          uint32_t split_idx;
          err = emit(c, NK_VM_OP_SPLIT, &split_idx);
          if (err != NK_SUCCESS) goto multi_fold_fail;
          c->program->states[split_idx].next = alt_init;
          if (first_split == NK_VM_STATE_NONE) first_split = split_idx;
          if (prev_split != NK_VM_STATE_NONE) c->program->states[prev_split].split_next = split_idx;
          prev_split = split_idx;
        } else {
          if (prev_split != NK_VM_STATE_NONE)
            c->program->states[prev_split].split_next = alt_init;
          else
            first_split = alt_init;
        }
      }

      // Step 3: merge all holes.
      err = hole_list_move_append(out_holes, &cc_holes);
      hole_list_free(&cc_holes);
      if (err != NK_SUCCESS) goto multi_fold_holes_fail;
      for (size_t i = 0; i < mf.count; i++) {
        err = hole_list_move_append(out_holes, &seq_holes[i]);
        hole_list_free(&seq_holes[i]);
        if (err != NK_SUCCESS) goto multi_fold_holes_fail;
      }

      *out_initial = first_split;
      return NK_SUCCESS;

    multi_fold_fail:
      hole_list_free(&cc_holes);
      for (size_t j = 0; j < compiled; j++) hole_list_free(&seq_holes[j]);
      return err;
    multi_fold_holes_fail:
      for (size_t j = 0; j < mf.count; j++) hole_list_free(&seq_holes[j]);
      return err;
    }
  }

  return compile_cc_state(c, &cc, out_initial, out_holes);
}

static nk_error_t
compile_char_type(compiler_t* c, const nk_node_t* node, uint32_t* out_initial, hole_list_t* out_holes) {
  const nk_char_type_node_t* char_type = &node->char_type;

  cc_set_t cc;
  nk_error_t err =
    cc_from_cprop(c, char_type_to_cprop(char_type->char_type), char_type->is_ascii_only, char_type->is_positive, &cc);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (char_type->is_ignore_case) {
    if ((char_type->fold_flags & NK_FOLD_ASCII_ONLY) != 0) {
      err = cc_ascii_fold_expand(&cc);
    } else {
      err = cc_simple_fold_expand(c->enc, char_type->fold_flags, &cc);
    }
    if (err != NK_SUCCESS) {
      cc_free(&cc);
      return err;
    }
  }

  return compile_cc_state(c, &cc, out_initial, out_holes);
}

static nk_error_t
compile_char_prop(compiler_t* c, const nk_node_t* node, uint32_t* out_initial, hole_list_t* out_holes) {
  const nk_char_prop_node_t* char_prop = &node->char_prop;

  cc_set_t cc;
  nk_error_t err = cc_from_cprop(c, char_prop->cprop, false, char_prop->is_positive, &cc);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (char_prop->is_ignore_case) {
    if ((char_prop->fold_flags & NK_FOLD_ASCII_ONLY) != 0) {
      err = cc_ascii_fold_expand(&cc);
    } else {
      err = cc_simple_fold_expand(c->enc, char_prop->fold_flags, &cc);
    }
    if (err != NK_SUCCESS) {
      cc_free(&cc);
      return err;
    }
  }

  return compile_cc_state(c, &cc, out_initial, out_holes);
}

static nk_error_t
compile_assertion(compiler_t* c, const nk_node_t* node, uint32_t* out_initial, hole_list_t* out_holes) {
  nk_assertion_type_t type = node->assertion.type;

  switch (type) {
    case NK_ASSERTION_TYPE_POSITIVE_LOOKAHEAD:
    case NK_ASSERTION_TYPE_NEGATIVE_LOOKAHEAD:
    case NK_ASSERTION_TYPE_POSITIVE_LOOKBEHIND:
    case NK_ASSERTION_TYPE_NEGATIVE_LOOKBEHIND:
    {
      // Compile the body into a self-contained sub-program, then emit a
      // zero-width LOOKAROUND state that runs it at match time.
      nk_program_t* inner_prog = NULL;
      size_t inner_error_offset = 0;
      size_t inner_error_length = 0;
      nk_error_t inner_err = nk_program_compile(
        c->enc,
        node->assertion.child,
        c->num_capture_groups,
        c->capture_entries,
        c->capture_entries_len,
        &inner_prog,
        &inner_error_offset,
        &inner_error_length
      );
      if (inner_err != NK_SUCCESS) {
        return inner_err;
      }

      // Append inner_prog to program->sub_programs[].
      size_t new_len = c->program->sub_programs_len + 1;
      nk_program_t** new_arr = (nk_program_t**)realloc(c->program->sub_programs, new_len * sizeof(nk_program_t*));
      if (new_arr == NULL) {
        nk_program_free(inner_prog);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      new_arr[c->program->sub_programs_len] = inner_prog;
      c->program->sub_programs = new_arr;
      uint32_t prog_idx = (uint32_t)c->program->sub_programs_len;
      c->program->sub_programs_len = new_len;

      // Emit the LOOKAROUND epsilon state.
      uint32_t la_idx;
      nk_error_t la_err = emit(c, NK_VM_OP_LOOKAROUND, &la_idx);
      if (la_err != NK_SUCCESS) {
        return la_err;
      }
      c->program->states[la_idx].lookaround_prog_idx = prog_idx;
      c->program->states[la_idx].lookaround_is_positive =
        (type == NK_ASSERTION_TYPE_POSITIVE_LOOKAHEAD || type == NK_ASSERTION_TYPE_POSITIVE_LOOKBEHIND);
      c->program->states[la_idx].lookaround_is_ahead =
        (type == NK_ASSERTION_TYPE_POSITIVE_LOOKAHEAD || type == NK_ASSERTION_TYPE_NEGATIVE_LOOKAHEAD);

      *out_initial = la_idx;
      return hole_list_push(out_holes, la_idx, false);
    }
    default:
      break;
  }

  uint32_t state_index;
  nk_error_t err = emit(c, NK_VM_OP_ASSERTION, &state_index);
  if (err != NK_SUCCESS) {
    return err;
  }
  c->program->states[state_index].assertion_type = type;

  *out_initial = state_index;
  return hole_list_push(out_holes, state_index, false);
}

static nk_error_t compile_capture(compiler_t* c, const nk_node_t* node, uint32_t* out_initial, hole_list_t* out_holes) {
  const nk_capture_node_t* capture = &node->capture;

  if (capture->capture_num == 0) {
    // No capture number assigned; behave like a plain group.
    return compile_node(c, capture->child, out_initial, out_holes);
  }

  uint32_t begin_index;
  nk_error_t err = emit(c, NK_VM_OP_CAP_BEGIN, &begin_index);
  if (err != NK_SUCCESS) {
    return err;
  }
  c->program->states[begin_index].cap_num = capture->capture_num;

  hole_list_t child_holes;
  hole_list_init(&child_holes);
  uint32_t child_initial;
  err = compile_node(c, capture->child, &child_initial, &child_holes);
  if (err != NK_SUCCESS) {
    hole_list_free(&child_holes);
    return err;
  }
  c->program->states[begin_index].next = child_initial;

  uint32_t end_index = NK_VM_STATE_NONE;
  err = emit(c, NK_VM_OP_CAP_END, &end_index);
  if (err == NK_SUCCESS) {
    c->program->states[end_index].cap_num = capture->capture_num;
    err = patch_all(c, &child_holes, end_index);
  }
  hole_list_free(&child_holes);
  if (err != NK_SUCCESS) {
    return err;
  }

  *out_initial = begin_index;
  return hole_list_push(out_holes, end_index, false);
}

static nk_error_t compile_concat(compiler_t* c, const nk_node_t* node, uint32_t* out_initial, hole_list_t* out_holes) {
  size_t children_len = node->concat.children_len;
  if (children_len == 0) {
    return compile_epsilon(c, out_initial, out_holes);
  }

  uint32_t initial = NK_VM_STATE_NONE;
  hole_list_t pending;
  hole_list_init(&pending);

  for (size_t i = 0; i < children_len; i++) {
    hole_list_t child_holes;
    hole_list_init(&child_holes);
    uint32_t child_initial;
    nk_error_t err = compile_node(c, node->concat.children[i], &child_initial, &child_holes);
    if (err == NK_SUCCESS) {
      if (initial == NK_VM_STATE_NONE) {
        initial = child_initial;
      } else {
        err = patch_all(c, &pending, child_initial);
      }
    }
    if (err == NK_SUCCESS) {
      err = hole_list_move_append(&pending, &child_holes);
    }
    hole_list_free(&child_holes);
    if (err != NK_SUCCESS) {
      hole_list_free(&pending);
      return err;
    }
  }

  *out_initial = initial;
  nk_error_t err = hole_list_move_append(out_holes, &pending);
  hole_list_free(&pending);
  return err;
}

static nk_error_t compile_alt(compiler_t* c, const nk_node_t* node, uint32_t* out_initial, hole_list_t* out_holes) {
  size_t children_len = node->alt.children_len;
  if (children_len == 0) {
    return compile_epsilon(c, out_initial, out_holes);
  }
  if (children_len == 1) {
    return compile_node(c, node->alt.children[0], out_initial, out_holes);
  }

  // A chain of SPLITs: each one tries its branch first (`next`) and falls
  // through to the rest of the alternation (`split_next`), preserving the
  // leftmost-first priority of the branches.
  uint32_t initial = NK_VM_STATE_NONE;
  uint32_t prev_split = NK_VM_STATE_NONE;
  for (size_t i = 0; i < children_len; i++) {
    bool is_last = i == children_len - 1;
    uint32_t split_index = NK_VM_STATE_NONE;
    if (!is_last) {
      nk_error_t err = emit(c, NK_VM_OP_SPLIT, &split_index);
      if (err != NK_SUCCESS) {
        return err;
      }
      if (initial == NK_VM_STATE_NONE) {
        initial = split_index;
      } else {
        c->program->states[prev_split].split_next = split_index;
      }
    }

    uint32_t child_initial;
    nk_error_t err = compile_node(c, node->alt.children[i], &child_initial, out_holes);
    if (err != NK_SUCCESS) {
      return err;
    }

    if (is_last) {
      c->program->states[prev_split].split_next = child_initial;
    } else {
      c->program->states[split_index].next = child_initial;
      prev_split = split_index;
    }
  }

  *out_initial = initial;
  return NK_SUCCESS;
}

static nk_error_t compile_zero_or_more(
  compiler_t* c,
  const nk_node_t* child,
  bool is_greedy,
  uint32_t* out_initial,
  hole_list_t* out_holes
) {
  uint32_t split_index;
  nk_error_t err = emit(c, NK_VM_OP_SPLIT, &split_index);
  if (err != NK_SUCCESS) {
    return err;
  }

  uint32_t body_initial;
  hole_list_t body_holes;
  hole_list_init(&body_holes);

  if (node_can_match_empty(child)) {
    // Guard the loop body so an iteration that matched the empty string
    // leaves the loop instead of spinning in the epsilon closure forever.
    if (c->next_epsilon_check_id >= NK_MAX_VM_EPSILON_CHECK_IDS) {
      return NK_ERR_PATTERN_TOO_COMPLEX;
    }
    uint32_t epsilon_check_id = c->next_epsilon_check_id++;

    uint32_t mark_index;
    err = emit(c, NK_VM_OP_MARK_EPSILON, &mark_index);
    if (err != NK_SUCCESS) {
      return err;
    }
    c->program->states[mark_index].check_id = epsilon_check_id;

    uint32_t child_initial;
    err = compile_node(c, child, &child_initial, &body_holes);
    if (err != NK_SUCCESS) {
      hole_list_free(&body_holes);
      return err;
    }
    c->program->states[mark_index].next = child_initial;

    uint32_t check_index;
    err = emit(c, NK_VM_OP_CHECK_EPSILON, &check_index);
    if (err != NK_SUCCESS) {
      hole_list_free(&body_holes);
      return err;
    }
    c->program->states[check_index].check_id = epsilon_check_id;
    err = patch_all(c, &body_holes, check_index);
    hole_list_free(&body_holes);
    if (err != NK_SUCCESS) {
      return err;
    }

    c->program->states[check_index].next = split_index;  // the body consumed: loop again
    err = hole_list_push(out_holes, check_index, true);  // the body was empty: leave the loop
    if (err != NK_SUCCESS) {
      return err;
    }
    body_initial = mark_index;
  } else {
    uint32_t child_initial;
    err = compile_node(c, child, &child_initial, &body_holes);
    if (err == NK_SUCCESS) {
      err = patch_all(c, &body_holes, split_index);  // loop back
    }
    hole_list_free(&body_holes);
    if (err != NK_SUCCESS) {
      return err;
    }
    body_initial = child_initial;
  }

  if (is_greedy) {
    c->program->states[split_index].next = body_initial;
    err = hole_list_push(out_holes, split_index, true);
  } else {
    c->program->states[split_index].split_next = body_initial;
    err = hole_list_push(out_holes, split_index, false);
  }
  if (err != NK_SUCCESS) {
    return err;
  }

  *out_initial = split_index;
  return NK_SUCCESS;
}

static nk_error_t compile_at_most_n(
  compiler_t* c,
  const nk_node_t* child,
  uint32_t n,
  bool is_greedy,
  uint32_t* out_initial,
  hole_list_t* out_holes
) {
  uint32_t initial = NK_VM_STATE_NONE;
  hole_list_t pending;
  hole_list_init(&pending);

  nk_error_t err = NK_SUCCESS;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t split_index;
    err = emit(c, NK_VM_OP_SPLIT, &split_index);
    if (err != NK_SUCCESS) {
      break;
    }
    if (initial == NK_VM_STATE_NONE) {
      initial = split_index;
    } else {
      err = patch_all(c, &pending, split_index);
      if (err != NK_SUCCESS) {
        break;
      }
    }

    uint32_t child_initial;
    err = compile_node(c, child, &child_initial, &pending);
    if (err != NK_SUCCESS) {
      break;
    }
    if (is_greedy) {
      c->program->states[split_index].next = child_initial;
      err = hole_list_push(out_holes, split_index, true);
    } else {
      c->program->states[split_index].split_next = child_initial;
      err = hole_list_push(out_holes, split_index, false);
    }
    if (err != NK_SUCCESS) {
      break;
    }
  }

  if (err == NK_SUCCESS) {
    err = hole_list_move_append(out_holes, &pending);
  }
  hole_list_free(&pending);
  if (err != NK_SUCCESS) {
    return err;
  }

  *out_initial = initial;
  return NK_SUCCESS;
}

static nk_error_t
compile_quantifier(compiler_t* c, const nk_node_t* node, uint32_t* out_initial, hole_list_t* out_holes) {
  const nk_quantifier_node_t* quantifier = &node->quantifier;

  if (quantifier->type == NK_QUANTIFIER_TYPE_POSSESSIVE) {
    // Compile the greedy version of this quantifier as a sub-program, then emit
    // a single NK_VM_OP_POSSESSIVE state that runs it at match time and commits
    // to the maximum match without offering any backtrack alternative.
    nk_node_t greedy_node = *node;
    greedy_node.quantifier.type = NK_QUANTIFIER_TYPE_GREEDY;
    nk_program_t* inner_prog = NULL;
    size_t inner_err_off = 0;
    size_t inner_err_len = 0;
    nk_error_t inner_err = nk_program_compile(
      c->enc,
      &greedy_node,
      c->num_capture_groups,
      c->capture_entries,
      c->capture_entries_len,
      &inner_prog,
      &inner_err_off,
      &inner_err_len
    );
    if (inner_err != NK_SUCCESS) {
      return inner_err;
    }
    size_t new_len = c->program->sub_programs_len + 1;
    nk_program_t** new_arr = (nk_program_t**)realloc(c->program->sub_programs, new_len * sizeof(nk_program_t*));
    if (new_arr == NULL) {
      nk_program_free(inner_prog);
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    new_arr[c->program->sub_programs_len] = inner_prog;
    c->program->sub_programs = new_arr;
    uint32_t prog_idx = (uint32_t)c->program->sub_programs_len;
    c->program->sub_programs_len = new_len;

    uint32_t poss_idx;
    nk_error_t pe = emit(c, NK_VM_OP_POSSESSIVE, &poss_idx);
    if (pe != NK_SUCCESS) {
      return pe;
    }
    c->program->states[poss_idx].possessive_prog_idx = prog_idx;
    *out_initial = poss_idx;
    return hole_list_push(out_holes, poss_idx, false);
  }
  bool is_greedy = quantifier->type == NK_QUANTIFIER_TYPE_GREEDY;

  uint32_t initial = NK_VM_STATE_NONE;
  hole_list_t pending;
  hole_list_init(&pending);

  nk_error_t err = NK_SUCCESS;

  // The mandatory part: `min` copies of the child in a row.
  for (uint32_t i = 0; i < quantifier->min && err == NK_SUCCESS; i++) {
    hole_list_t child_holes;
    hole_list_init(&child_holes);
    uint32_t child_initial;
    err = compile_node(c, quantifier->child, &child_initial, &child_holes);
    if (err == NK_SUCCESS) {
      if (initial == NK_VM_STATE_NONE) {
        initial = child_initial;
      } else {
        err = patch_all(c, &pending, child_initial);
      }
    }
    if (err == NK_SUCCESS) {
      err = hole_list_move_append(&pending, &child_holes);
    }
    hole_list_free(&child_holes);
  }

  // The optional part: `{min,max}` appends an at-most-`max - min` chain and
  // `{min,}` appends a loop.
  if (err == NK_SUCCESS && (!quantifier->has_max || quantifier->max > quantifier->min)) {
    hole_list_t tail_holes;
    hole_list_init(&tail_holes);
    uint32_t tail_initial;
    if (quantifier->has_max) {
      err = compile_at_most_n(
        c,
        quantifier->child,
        quantifier->max - quantifier->min,
        is_greedy,
        &tail_initial,
        &tail_holes
      );
    } else {
      err = compile_zero_or_more(c, quantifier->child, is_greedy, &tail_initial, &tail_holes);
    }
    if (err == NK_SUCCESS) {
      if (initial == NK_VM_STATE_NONE) {
        initial = tail_initial;
      } else {
        err = patch_all(c, &pending, tail_initial);
      }
    }
    if (err == NK_SUCCESS) {
      err = hole_list_move_append(&pending, &tail_holes);
    }
    hole_list_free(&tail_holes);
  }

  // `{0}` and `{0,0}`: the whole quantifier matches the empty string.
  if (err == NK_SUCCESS && initial == NK_VM_STATE_NONE) {
    err = compile_epsilon(c, &initial, &pending);
  }

  if (err == NK_SUCCESS) {
    err = hole_list_move_append(out_holes, &pending);
  }
  hole_list_free(&pending);
  if (err != NK_SUCCESS) {
    return err;
  }

  *out_initial = initial;
  return NK_SUCCESS;
}

static nk_error_t
compile_node_dispatch(compiler_t* c, const nk_node_t* node, uint32_t* out_initial, hole_list_t* out_holes) {
  switch (node->base.type) {
    case NK_NODE_TYPE_LITERAL:
      return compile_literal(c, node, out_initial, out_holes);
    case NK_NODE_TYPE_CHAR_CLASS:
      return compile_char_class(c, node, out_initial, out_holes);
    case NK_NODE_TYPE_CHAR_TYPE:
      return compile_char_type(c, node, out_initial, out_holes);
    case NK_NODE_TYPE_CHAR_PROP:
      return compile_char_prop(c, node, out_initial, out_holes);
    case NK_NODE_TYPE_DOT:
    {
      uint32_t state_index;
      nk_error_t err = emit(c, NK_VM_OP_DOT, &state_index);
      if (err != NK_SUCCESS) {
        return err;
      }
      c->program->states[state_index].allows_newline = node->dot.allows_newline;
      *out_initial = state_index;
      return hole_list_push(out_holes, state_index, false);
    }
    case NK_NODE_TYPE_KEEP:
    {
      uint32_t state_index;
      nk_error_t err = emit(c, NK_VM_OP_KEEP, &state_index);
      if (err != NK_SUCCESS) {
        return err;
      }
      *out_initial = state_index;
      return hole_list_push(out_holes, state_index, false);
    }
    case NK_NODE_TYPE_ASSERTION:
      return compile_assertion(c, node, out_initial, out_holes);
    case NK_NODE_TYPE_QUANTIFIER:
      return compile_quantifier(c, node, out_initial, out_holes);
    case NK_NODE_TYPE_CAPTURE:
      return compile_capture(c, node, out_initial, out_holes);
    case NK_NODE_TYPE_GROUP:
      return compile_node(c, node->group.child, out_initial, out_holes);
    case NK_NODE_TYPE_CONCAT:
      return compile_concat(c, node, out_initial, out_holes);
    case NK_NODE_TYPE_ALT:
      return compile_alt(c, node, out_initial, out_holes);
    case NK_NODE_TYPE_BACK_REF:
    {
      const nk_back_ref_node_t* back_ref = &node->back_ref;
      size_t entry_index = back_ref->resolved_capture_name_map_entry_index;
      size_t count = back_ref->resolved_capture_num_count;

      if (count == 0 || entry_index == NK_CAPTURE_NAME_MAP_ENTRY_INDEX_UNRESOLVED) {
        return NK_ERR_UNDEFINED_BACK_REF;
      }

      // `entry_index` means different things per target_kind:
      //   CAPTURE_NUM: directly the 0-based capture index (capture_num - 1); no capture_entries lookup.
      //   NAME:        an index into capture_entries[] (the named group map).
      const nk_capture_entry_t* entry = NULL;
      uint32_t single_cap_num = 0;
      if (back_ref->target_kind == NK_REF_TARGET_KIND_CAPTURE_NUM) {
        single_cap_num = back_ref->capture_num;
      } else {
        if (c->capture_entries == NULL || entry_index >= c->capture_entries_len) {
          return NK_ERR_UNDEFINED_BACK_REF;
        }
        entry = &c->capture_entries[entry_index];
      }

      // For multiple same-name captures, build a SPLIT chain in reverse order
      // (last-defined group has highest priority — resolves onigmo_bugs.md BR1).
      uint32_t initial = NK_VM_STATE_NONE;
      uint32_t prev_split = NK_VM_STATE_NONE;
      for (size_t i = 0; i < count; i++) {
        // Reverse: index 0 tries the last-defined capture group first.
        uint32_t cap_num = (back_ref->target_kind == NK_REF_TARGET_KIND_CAPTURE_NUM)
                             ? single_cap_num
                             : entry->capture_nums[count - 1 - i];
        bool is_last = (i == count - 1);

        uint32_t br_idx;
        nk_error_t br_err;
        if (!is_last) {
          uint32_t split_idx;
          br_err = emit(c, NK_VM_OP_SPLIT, &split_idx);
          if (br_err != NK_SUCCESS) return br_err;
          if (initial == NK_VM_STATE_NONE) initial = split_idx;
          if (prev_split != NK_VM_STATE_NONE) {
            c->program->states[prev_split].split_next = split_idx;
          }
          prev_split = split_idx;

          br_err = emit(c, NK_VM_OP_BACK_REF, &br_idx);
          if (br_err != NK_SUCCESS) return br_err;
          c->program->states[split_idx].next = br_idx;
        } else {
          br_err = emit(c, NK_VM_OP_BACK_REF, &br_idx);
          if (br_err != NK_SUCCESS) return br_err;
          if (initial == NK_VM_STATE_NONE) initial = br_idx;
          if (prev_split != NK_VM_STATE_NONE) {
            c->program->states[prev_split].split_next = br_idx;
          }
        }

        c->program->states[br_idx].cap_num = cap_num;
        c->program->states[br_idx].is_ignore_case = back_ref->is_ignore_case;
        c->program->states[br_idx].fold_flags = back_ref->fold_flags;
        br_err = hole_list_push(out_holes, br_idx, false);
        if (br_err != NK_SUCCESS) return br_err;
      }

      *out_initial = initial;
      c->program->has_back_refs = true;
      return NK_SUCCESS;
    }
    case NK_NODE_TYPE_CALL:
      return NK_ERR_UNSUPPORTED_SUBEXP_CALL;
    case NK_NODE_TYPE_ATOMIC:
      return NK_ERR_UNSUPPORTED_ATOMIC_GROUP;
    case NK_NODE_TYPE_ABSENCE:
      return NK_ERR_UNSUPPORTED_ABSENCE_GROUP;
    case NK_NODE_TYPE_CONDITIONAL:
      return NK_ERR_UNSUPPORTED_CONDITIONAL;
    case NK_NODE_TYPE_UNKNOWN:
    case NK_NODE_TYPE_NEWLINE:
    case NK_NODE_TYPE_GRAPHEME_CLUSTER:
      return NK_ERR_UNSUPPORTED_FEATURE;
  }

  return NK_ERR_INTERNAL_ERROR;
}

static nk_error_t compile_node(compiler_t* c, const nk_node_t* node, uint32_t* out_initial, hole_list_t* out_holes) {
  if (node == NULL) {
    return compile_epsilon(c, out_initial, out_holes);
  }

  nk_error_t err = compile_node_dispatch(c, node, out_initial, out_holes);
  if (err != NK_SUCCESS && !c->has_error_span) {
    // The innermost failing node records its span; outer nodes keep it.
    c->has_error_span = true;
    c->error_offset = node->base.span_offset;
    c->error_length = node->base.span_length;
  }
  return err;
}

// ============================================================================
//
// Public API:
//
// ============================================================================

// Returns the leading case-sensitive literal bytes of `node` (skipping any
// leading zero-width assertions). Returns NULL when there is no such prefix or
// when any byte is non-ASCII (>= 0x80); the VM pre-scan relies on the
// all-ASCII guarantee for encoding safety across UTF-8 / Shift-JIS etc.
static const uint8_t* node_literal_prefix(const nk_node_t* node, size_t* out_len) {
  if (node == NULL) {
    *out_len = 0;
    return NULL;
  }
  switch (node->base.type) {
    case NK_NODE_TYPE_LITERAL:
      if (node->literal.is_ignore_case) {
        *out_len = 0;
        return NULL;
      }
      {
        size_t len = (size_t)(node->literal.buf.bytes_end - node->literal.buf.bytes);
        for (size_t i = 0; i < len; i++) {
          if (node->literal.buf.bytes[i] >= 0x80u) {
            *out_len = 0;
            return NULL;
          }
        }
        *out_len = len;
        return node->literal.buf.bytes;
      }
    case NK_NODE_TYPE_CONCAT:
      for (size_t i = 0; i < node->concat.children_len; i++) {
        if (node->concat.children[i]->base.type == NK_NODE_TYPE_ASSERTION) {
          continue;  // zero-width: look past it
        }
        return node_literal_prefix(node->concat.children[i], out_len);
      }
      *out_len = 0;
      return NULL;
    case NK_NODE_TYPE_CAPTURE:
      return node_literal_prefix(node->capture.child, out_len);
    case NK_NODE_TYPE_GROUP:
      return node_literal_prefix(node->group.child, out_len);
    default:
      *out_len = 0;
      return NULL;
  }
}

// Unwraps one or more layers of CAPTURE / GROUP to reach the inner node.
static const nk_node_t* node_unwrap(const nk_node_t* node) {
  while (node != NULL) {
    if (node->base.type == NK_NODE_TYPE_CAPTURE) {
      node = node->capture.child;
    } else if (node->base.type == NK_NODE_TYPE_GROUP) {
      node = node->group.child;
    } else {
      break;
    }
  }
  return node;
}

// Returns true when `node` (after unwrapping) is a char-class–like AST node
// (NK_NODE_TYPE_CHAR_CLASS, CHAR_TYPE, or CHAR_PROP) — all of which compile to
// NK_VM_OP_CHAR_CLASS in the VM.
static bool node_is_char_class_like(const nk_node_t* node) {
  node = node_unwrap(node);
  if (node == NULL) return false;
  return node->base.type == NK_NODE_TYPE_CHAR_CLASS || node->base.type == NK_NODE_TYPE_CHAR_TYPE ||
         node->base.type == NK_NODE_TYPE_CHAR_PROP;
}

// Returns true when `node` (after unwrapping captures/groups) is a
// case-sensitive, all-ASCII literal.  Writes its length to `*out_len` and
// a pointer to its bytes to `*out_bytes` (pointing into the AST; do not free).
static bool node_is_ascii_literal(const nk_node_t* node, const uint8_t** out_bytes, size_t* out_len) {
  node = node_unwrap(node);
  if (node == NULL || node->base.type != NK_NODE_TYPE_LITERAL) return false;
  if (node->literal.is_ignore_case) return false;
  size_t len = (size_t)(node->literal.buf.bytes_end - node->literal.buf.bytes);
  for (size_t i = 0; i < len; i++) {
    if (node->literal.buf.bytes[i] >= 0x80u) return false;
  }
  *out_bytes = node->literal.buf.bytes;
  *out_len = len;
  return true;
}

// Like node_is_ascii_literal but allows non-ASCII bytes.
static bool node_is_literal(const nk_node_t* node, const uint8_t** out_bytes, size_t* out_len) {
  node = node_unwrap(node);
  if (node == NULL || node->base.type != NK_NODE_TYPE_LITERAL) return false;
  if (node->literal.is_ignore_case) return false;
  *out_bytes = node->literal.buf.bytes;
  *out_len = (size_t)(node->literal.buf.bytes_end - node->literal.buf.bytes);
  return *out_len > 0;
}

// Populates `program->full_alt_*` when `root` is a top-level literal or
// alternation of literals (any encoding, including non-ASCII) with no
// capture groups.  Only called when the ASCII-only paths are not applicable.
static nk_error_t extract_full_alt_literals(nk_program_t* program, const nk_node_t* root) {
  const nk_node_t* node = node_unwrap(root);
  if (node == NULL) return NK_SUCCESS;

  size_t count;
  if (node->base.type == NK_NODE_TYPE_LITERAL) {
    count = 1;
  } else if (node->base.type == NK_NODE_TYPE_ALT) {
    count = node->alt.children_len;
    if (count < 2u) return NK_SUCCESS;
  } else {
    return NK_SUCCESS;
  }

  // Verify all branches are case-sensitive literals (any encoding).
  for (size_t i = 0; i < count; i++) {
    const nk_node_t* child = (node->base.type == NK_NODE_TYPE_LITERAL) ? node : node->alt.children[i];
    const uint8_t* dummy;
    size_t dlen;
    if (!node_is_literal(child, &dummy, &dlen)) return NK_SUCCESS;
  }

  uint8_t** bytes_arr = (uint8_t**)malloc(count * sizeof(uint8_t*));
  size_t* lens_arr = (size_t*)malloc(count * sizeof(size_t));
  if (bytes_arr == NULL || lens_arr == NULL) {
    free(bytes_arr);
    free(lens_arr);
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  for (size_t i = 0; i < count; i++) {
    const nk_node_t* child = (node->base.type == NK_NODE_TYPE_LITERAL) ? node : node->alt.children[i];
    const uint8_t* src;
    size_t len;
    node_is_literal(child, &src, &len);
    uint8_t* copy = (uint8_t*)malloc(len);
    if (copy == NULL) {
      for (size_t j = 0; j < i; j++) free(bytes_arr[j]);
      free(bytes_arr);
      free(lens_arr);
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    memcpy(copy, src, len);
    bytes_arr[i] = copy;
    lens_arr[i] = len;
  }

  program->full_alt_bytes = bytes_arr;
  program->full_alt_lens = lens_arr;
  program->full_alt_count = count;
  return NK_SUCCESS;
}

// Attempts to populate `program->alt_literal_*` when `root` is a top-level
// alternation (possibly wrapped in captures/groups) whose every branch is a
// case-sensitive, all-ASCII literal.  Returns true on success; leaves the
// fields NULL/0 on failure.
static nk_error_t extract_alt_literals(nk_program_t* program, const nk_node_t* root) {
  const nk_node_t* node = node_unwrap(root);
  if (node == NULL || node->base.type != NK_NODE_TYPE_ALT) return NK_SUCCESS;
  if (node->alt.children_len < 2u) return NK_SUCCESS;

  for (size_t i = 0; i < node->alt.children_len; i++) {
    const uint8_t* dummy_bytes;
    size_t dummy_len;
    if (!node_is_ascii_literal(node->alt.children[i], &dummy_bytes, &dummy_len)) return NK_SUCCESS;
  }

  size_t count = node->alt.children_len;
  uint8_t** bytes_arr = (uint8_t**)malloc(count * sizeof(uint8_t*));
  size_t* lens_arr = (size_t*)malloc(count * sizeof(size_t));
  if (bytes_arr == NULL || lens_arr == NULL) {
    free(bytes_arr);
    free(lens_arr);
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  for (size_t i = 0; i < count; i++) {
    const uint8_t* src;
    size_t len;
    node_is_ascii_literal(node->alt.children[i], &src, &len);
    uint8_t* copy = (uint8_t*)malloc(len > 0u ? len : 1u);
    if (copy == NULL) {
      for (size_t j = 0; j < i; j++) free(bytes_arr[j]);
      free(bytes_arr);
      free(lens_arr);
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    if (len > 0u) memcpy(copy, src, len);
    bytes_arr[i] = copy;
    lens_arr[i] = len;
  }

  program->alt_literal_bytes = bytes_arr;
  program->alt_literal_lens = lens_arr;
  program->alt_literal_count = count;
  return NK_SUCCESS;
}

// Returns the first ASCII byte that is guaranteed to appear in any match of
// `node`.  Callers use this for a prefilter: if the byte is absent from the
// subject, no match is possible and the search can exit immediately.
//
// For NK_NODE_TYPE_CONCAT: walk children left to right, skipping zero-width
// nodes (assertions, \K) and children that can match empty.  Return the first
// required byte found in a non-optional child.  Continuing past a child with
// no extractable byte (e.g., a char class) is intentional: subsequent children
// are guaranteed to run too, so their required bytes are equally valid.
static bool node_required_byte(const nk_node_t* node, uint8_t* out) {
  if (node == NULL) return false;
  switch (node->base.type) {
    case NK_NODE_TYPE_LITERAL:
    {
      if (node->literal.is_ignore_case) return false;
      size_t len = (size_t)(node->literal.buf.bytes_end - node->literal.buf.bytes);
      for (size_t i = 0; i < len; i++) {
        uint8_t b = node->literal.buf.bytes[i];
        if (b < 0x80u) {
          *out = b;
          return true;
        }
      }
      return false;
    }
    case NK_NODE_TYPE_CONCAT:
      for (size_t i = 0; i < node->concat.children_len; i++) {
        const nk_node_t* child = node->concat.children[i];
        if (child->base.type == NK_NODE_TYPE_ASSERTION || child->base.type == NK_NODE_TYPE_KEEP) continue;
        if (node_can_match_empty(child)) continue;
        if (node_required_byte(child, out)) return true;
      }
      return false;
    case NK_NODE_TYPE_ALT:
    {
      if (node->alt.children_len == 0) return false;
      uint8_t common;
      if (!node_required_byte(node->alt.children[0], &common)) return false;
      for (size_t i = 1; i < node->alt.children_len; i++) {
        uint8_t b;
        if (!node_required_byte(node->alt.children[i], &b) || b != common) return false;
      }
      *out = common;
      return true;
    }
    case NK_NODE_TYPE_QUANTIFIER:
      return node->quantifier.min > 0 && node_required_byte(node->quantifier.child, out);
    case NK_NODE_TYPE_CAPTURE:
      return node_required_byte(node->capture.child, out);
    case NK_NODE_TYPE_GROUP:
      return node_required_byte(node->group.child, out);
    case NK_NODE_TYPE_ATOMIC:
      return node_required_byte(node->atomic.child, out);
    default:
      return false;
  }
}

// Returns true when the pattern is guaranteed to only match at position 0
// (i.e., the effective first token is a \A assertion). Used to set
// `program->is_anchored` so the VM can skip re-injection at later positions.
static bool node_starts_with_string_anchor(const nk_node_t* node) {
  if (node == NULL) {
    return false;
  }
  switch (node->base.type) {
    case NK_NODE_TYPE_ASSERTION:
      return node->assertion.type == NK_ASSERTION_TYPE_BEGIN_OF_STRING;
    case NK_NODE_TYPE_CONCAT:
      return node->concat.children_len > 0 && node_starts_with_string_anchor(node->concat.children[0]);
    case NK_NODE_TYPE_CAPTURE:
      return node_starts_with_string_anchor(node->capture.child);
    case NK_NODE_TYPE_GROUP:
      return node_starts_with_string_anchor(node->group.child);
    default:
      return false;
  }
}

// ============================================================================
//
// Thompson NFA bitset precomputation (used by the fast boolean match? path):
//
// ============================================================================

// DFS through epsilon-only transitions starting from `start_idx`.
// Returns a bitmask of consuming-state indices reached, with NK_BITSET_MATCH_BIT
// set if any MATCH state is reachable.  ASSERTION and KEEP states are treated
// as transparent epsilons (they must have been screened out before calling).
// For CHECK_EPSILON we follow only `next` (body-not-empty branch); SPLIT
// follows both branches.
static uint64_t
compute_epsilon_mask(const nk_vm_state_t* states, uint32_t num_states, uint32_t start_idx, uint8_t* visited) {
  if (start_idx == NK_VM_STATE_NONE || start_idx >= num_states) {
    return 0;
  }

  uint64_t result = 0;
  uint32_t stack[256];
  size_t top = 0;
  stack[top++] = start_idx;

  while (top > 0) {
    uint32_t idx = stack[--top];
    if (idx >= num_states || visited[idx]) {
      continue;
    }
    visited[idx] = 1;

    const nk_vm_state_t* s = &states[idx];
    switch (s->op) {
      case NK_VM_OP_CODE:
      case NK_VM_OP_CHAR_CLASS:
      case NK_VM_OP_DOT:
        result |= (uint64_t)1u << idx;
        break;
      case NK_VM_OP_MATCH:
        result |= NK_BITSET_MATCH_BIT;
        break;
      case NK_VM_OP_SPLIT:
        if (s->split_next != NK_VM_STATE_NONE) {
          stack[top++] = s->split_next;
        }
        if (s->next != NK_VM_STATE_NONE) {
          stack[top++] = s->next;
        }
        break;
      case NK_VM_OP_CHECK_EPSILON:
        // Assume body-not-empty (we're computing "after consuming a char"):
        // take `next` (loop path) only.  SPLIT inside the loop provides the
        // exit path so MATCH is still discovered through the loop → SPLIT.
        if (s->next != NK_VM_STATE_NONE) {
          stack[top++] = s->next;
        }
        break;
      default:
        // CAP_BEGIN, CAP_END, KEEP, JUMP, MARK_EPSILON, CHECK_VISITED,
        // ASSERTION: all follow `next` only.
        if (s->next != NK_VM_STATE_NONE) {
          stack[top++] = s->next;
        }
        break;
    }
  }
  return result;
}

// Precompute `goto_mask` and `initial_mask` for the Thompson NFA bitset path.
// Sets program->goto_mask = NULL if the program is not eligible (too large,
// or contains ASSERTION / KEEP states).
static void compute_goto_masks(nk_program_t* program) {
  uint32_t n = (uint32_t)program->states_len;

  // Only applicable for small programs (bit 63 is reserved for MATCH).
  if (n > 63) {
    return;
  }
  // Assertion, \K, and BACK_REF states depend on position or captured content.
  for (uint32_t i = 0; i < n; i++) {
    nk_vm_op_t op = program->states[i].op;
    if (op == NK_VM_OP_ASSERTION || op == NK_VM_OP_KEEP || op == NK_VM_OP_BACK_REF || op == NK_VM_OP_LOOKAROUND ||
        op == NK_VM_OP_POSSESSIVE) {
      return;
    }
  }

  program->goto_mask = (uint64_t*)calloc(n, sizeof(uint64_t));
  if (program->goto_mask == NULL) {
    return;
  }

  uint8_t* visited = (uint8_t*)calloc(n, 1);
  if (visited == NULL) {
    free(program->goto_mask);
    program->goto_mask = NULL;
    return;
  }

  for (uint32_t i = 0; i < n; i++) {
    nk_vm_op_t op = program->states[i].op;
    if (op != NK_VM_OP_CODE && op != NK_VM_OP_CHAR_CLASS && op != NK_VM_OP_DOT) {
      continue;
    }
    uint32_t next_idx = program->states[i].next;
    memset(visited, 0, (size_t)n);
    program->goto_mask[i] = compute_epsilon_mask(program->states, n, next_idx, visited);
  }

  memset(visited, 0, (size_t)n);
  program->initial_mask = compute_epsilon_mask(program->states, n, program->initial_state, visited);

  free(visited);

  program->lazy_dfa = (nk_lazy_dfa_t*)calloc(1u, sizeof(nk_lazy_dfa_t));
  // calloc zeroes all bytes, so all slots start with occupied==0 (empty).
}

// Populates `program->first_byte_table` and `program->first_byte_table_valid`.
// The table encodes which ASCII bytes can be the first character consumed from
// the NFA's initial state, enabling search_impl_bitset to skip positions that
// cannot possibly begin a match.
//
// Safety: when `active == initial_mask` and `curr_byte` is NOT in the table,
// all consuming states in initial_mask fail to match `curr_byte` (since the
// table is their ASCII union), so bitset_transition returns 0 and re-injection
// restores initial_mask unchanged.  Skipping that byte is therefore
// equivalent to running the full transition — loop-back structure is irrelevant.
//
// The table is only applicable when goto_mask is set and at least one ASCII
// byte can begin a match (CODE < 128 or CHAR_CLASS with non-empty ascii_lookup).
// DOT states disable the table because they match almost all bytes.
static void compute_first_byte_table(nk_program_t* program) {
  if (program->goto_mask == NULL) {
    return;
  }

  uint32_t n = (uint32_t)program->states_len;
  uint64_t imask = program->initial_mask & ~NK_BITSET_MATCH_BIT;
  if (imask == 0u) {
    return;
  }

  uint8_t table[128];
  memset(table, 0, sizeof(table));
  bool can_jump = true;
  bool has_any = false;

  for (uint32_t idx = 0; idx < n && can_jump; idx++) {
    if (!(imask & ((uint64_t)1u << idx))) {
      continue;
    }
    const nk_vm_state_t* s = &program->states[idx];

    if (s->op == NK_VM_OP_CODE) {
      // Non-ASCII CODE states are safe: the jump is guarded by `curr_code < 128`
      // so non-ASCII code points never trigger the scan.
      if (s->code < 128u) {
        table[(uint8_t)s->code] = 1u;
        has_any = true;
        // ASCII-only fold: the case-folded counterpart is also a valid first byte.
        if ((s->fold_flags & NK_FOLD_ASCII_ONLY) != 0) {
          uint8_t b = (uint8_t)s->code;
          uint8_t folded = (b >= 'A' && b <= 'Z')   ? (uint8_t)(b | 0x20u)
                           : (b >= 'a' && b <= 'z') ? (uint8_t)(b & ~0x20u)
                                                    : b;
          if (folded != b) {
            table[folded] = 1u;
          }
        }
      }
    } else if (s->op == NK_VM_OP_CHAR_CLASS) {
      const nk_vm_char_class_t* cc = &program->char_classes[s->char_class_index];
      // Non-ASCII ranges are harmless: the jump is guarded by `curr_code < 128`.
      for (uint32_t b = 0u; b < 128u; b++) {
        if (cc->ascii_lookup[b] != 0u) {
          table[(uint8_t)b] = 1u;
          has_any = true;
        }
      }
    } else {
      // DOT matches nearly all bytes — the table would cover everything, skip.
      can_jump = false;
    }
  }

  if (!can_jump || !has_any) {
    return;
  }

  memcpy(program->first_byte_table, table, sizeof(table));
  program->first_byte_table_valid = true;
}

nk_error_t nk_program_compile(
  const nk_encoding_t* enc,
  const nk_node_t* root_node,
  uint32_t num_capture_groups,
  const nk_capture_entry_t* capture_entries,
  size_t capture_entries_len,
  nk_program_t** out_program,
  size_t* out_error_offset,
  size_t* out_error_length
) {
  nk_program_t* program = (nk_program_t*)malloc(sizeof(nk_program_t));
  if (program == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  program->enc = enc;
  program->states = NULL;
  program->states_len = 0;
  program->initial_state = 0;
  program->num_capture_groups = num_capture_groups;
  program->num_check_ids = 0;
  program->num_epsilon_check_ids = 0;
  program->char_classes = NULL;
  program->char_classes_len = 0;
  program->is_anchored = false;
  program->is_pure_literal = false;
  program->is_pure_alt_literal = false;
  program->full_alt_bytes = NULL;
  program->full_alt_lens = NULL;
  program->full_alt_count = 0u;
  program->literal_prefix_bytes = NULL;
  program->literal_prefix_len = 0;
  program->has_required_byte = false;
  program->required_byte = 0u;
  program->alt_literal_bytes = NULL;
  program->alt_literal_lens = NULL;
  program->alt_literal_count = 0u;
  program->goto_mask = NULL;
  program->initial_mask = 0;
  program->lazy_dfa = NULL;
  program->first_byte_table_valid = false;
  memset(program->first_byte_table, 0, sizeof(program->first_byte_table));
  program->is_pure_char_class_plus = false;
  program->pure_cc_index = 0u;
  program->has_back_refs = false;
  program->sub_programs = NULL;
  program->sub_programs_len = 0;

  compiler_t compiler = {
    .enc = enc,
    .program = program,
    .states_cap = 0,
    .char_classes_cap = 0,
    .next_check_id = 0,
    .next_epsilon_check_id = 0,
    .error_offset = 0,
    .error_length = 0,
    .has_error_span = false,
    .capture_entries = capture_entries,
    .capture_entries_len = capture_entries_len,
    .num_capture_groups = num_capture_groups,
  };

  hole_list_t holes;
  hole_list_init(&holes);

  // The whole program is `CAP_BEGIN(0) -> <root> -> CAP_END(0) -> MATCH`.
  uint32_t begin_index = 0;
  nk_error_t err = emit(&compiler, NK_VM_OP_CAP_BEGIN, &begin_index);

  if (err == NK_SUCCESS) {
    if (root_node != NULL) {
      uint32_t root_initial;
      err = compile_node(&compiler, root_node, &root_initial, &holes);
      if (err == NK_SUCCESS) {
        program->states[begin_index].next = root_initial;
      }
    } else {
      err = hole_list_push(&holes, begin_index, false);
    }
  }

  uint32_t end_index = 0;
  if (err == NK_SUCCESS) {
    err = emit(&compiler, NK_VM_OP_CAP_END, &end_index);
  }
  if (err == NK_SUCCESS) {
    err = patch_all(&compiler, &holes, end_index);
  }
  uint32_t match_index = 0;
  if (err == NK_SUCCESS) {
    err = emit(&compiler, NK_VM_OP_MATCH, &match_index);
  }

  hole_list_free(&holes);

  if (err != NK_SUCCESS) {
    if (out_error_offset != NULL) {
      *out_error_offset = compiler.has_error_span ? compiler.error_offset : 0;
    }
    if (out_error_length != NULL) {
      *out_error_length = compiler.has_error_span ? compiler.error_length : 0;
    }
    nk_program_free(program);
    return err;
  }

  program->states[end_index].next = match_index;
  program->initial_state = begin_index;
  program->num_check_ids = compiler.next_check_id;
  program->num_epsilon_check_ids = compiler.next_epsilon_check_id;
  program->is_anchored = node_starts_with_string_anchor(root_node);
  {
    size_t prefix_len = 0;
    const uint8_t* prefix_bytes = node_literal_prefix(root_node, &prefix_len);
    if (prefix_len > 0) {
      uint8_t* copy = (uint8_t*)malloc(prefix_len);
      if (copy == NULL) {
        nk_program_free(program);
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
      memcpy(copy, prefix_bytes, prefix_len);
      program->literal_prefix_bytes = copy;
      program->literal_prefix_len = prefix_len;
    }
  }

  // Pure literal: the entire pattern is a single case-sensitive ASCII literal.
  // When true, the VM is bypassed in search_impl — memmem alone resolves the match.
  if (program->literal_prefix_len > 0) {
    const uint8_t* lit_bytes;
    size_t lit_len;
    program->is_pure_literal = node_is_ascii_literal(root_node, &lit_bytes, &lit_len);
  }

  if (root_node != NULL && !program->is_anchored && program->literal_prefix_len == 0) {
    // Multi-literal alternation prefilter: if every branch is a plain ASCII
    // literal, store all alternatives for a multi-memmem pre-scan in the VM.
    {
      nk_error_t alt_err = extract_alt_literals(program, root_node);
      if (alt_err != NK_SUCCESS) {
        nk_program_free(program);
        return alt_err;
      }
    }

    // Pure alternation flag: the entire pattern is an alternation of ASCII literals
    // with no capture groups. The VM is bypassed — the alt pre-scan result alone
    // resolves the match (analogous to is_pure_literal for the single-literal case).
    program->is_pure_alt_literal = (program->alt_literal_count > 0u && program->num_capture_groups == 0u);

    // Non-ASCII literal/alternation bypass: handles pure literals or alternations
    // whose branches contain non-ASCII bytes (not covered by the ASCII-only paths).
    if (!program->is_pure_alt_literal && program->num_capture_groups == 0u) {
      nk_error_t full_err = extract_full_alt_literals(program, root_node);
      if (full_err != NK_SUCCESS) {
        nk_program_free(program);
        return full_err;
      }
    }

    // Required-byte prefilter: find a single ASCII byte guaranteed to appear in
    // any match.  Only useful when neither a literal prefix nor an alt-literal
    // prefilter is available (both are stricter).
    if (program->alt_literal_count == 0u && program->full_alt_count == 0u) {
      uint8_t req = 0u;
      if (node_required_byte(root_node, &req)) {
        program->has_required_byte = true;
        program->required_byte = req;
      }
    }
  }

  compute_goto_masks(program);
  compute_first_byte_table(program);

  // Pure char-class loop bypass: detect [X]+, \w+, \d+ etc. with no captures.
  // Requires states[] to be finalised (after compile), so runs after goto_masks.
  if (root_node != NULL && !program->is_anchored && program->num_capture_groups == 0u) {
    const nk_node_t* r = node_unwrap(root_node);
    if (r != NULL && r->base.type == NK_NODE_TYPE_QUANTIFIER && r->quantifier.min >= 1u &&
        node_is_char_class_like(r->quantifier.child)) {
      for (size_t si = 0; si < program->states_len; si++) {
        if (program->states[si].op == NK_VM_OP_CHAR_CLASS) {
          uint32_t ci = program->states[si].char_class_index;
          const nk_vm_char_class_t* cc = &program->char_classes[ci];
          bool has_ascii = false;
          for (uint32_t b = 0; b < 128u; b++) {
            if (cc->ascii_lookup[b] != 0u) {
              has_ascii = true;
              break;
            }
          }
          if (has_ascii) {
            program->is_pure_char_class_plus = true;
            program->pure_cc_index = ci;
          }
          break;
        }
      }
    }
  }

  *out_program = program;
  return NK_SUCCESS;
}

void nk_program_free(nk_program_t* program) {
  if (program == NULL) {
    return;
  }

  free(program->states);
  for (size_t i = 0; i < program->char_classes_len; i++) {
    free(program->char_classes[i].ranges);
  }
  free(program->char_classes);
  free(program->literal_prefix_bytes);
  if (program->alt_literal_bytes != NULL) {
    for (size_t i = 0; i < program->alt_literal_count; i++) {
      free(program->alt_literal_bytes[i]);
    }
    free(program->alt_literal_bytes);
    free(program->alt_literal_lens);
  }
  if (program->full_alt_bytes != NULL) {
    for (size_t i = 0; i < program->full_alt_count; i++) {
      free(program->full_alt_bytes[i]);
    }
    free(program->full_alt_bytes);
    free(program->full_alt_lens);
  }
  free(program->goto_mask);
  free(program->lazy_dfa);
  for (size_t i = 0; i < program->sub_programs_len; i++) {
    nk_program_free(program->sub_programs[i]);
  }
  free(program->sub_programs);
  free(program);
}

nk_error_t nk_region_init(nk_region_t* region, uint32_t num_capture_groups) {
  region->num_caps = 0;
  region->caps = NULL;

  size_t num_caps = (size_t)num_capture_groups + 1;
  size_t* caps = (size_t*)malloc(num_caps * 2 * sizeof(size_t));
  if (caps == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  for (size_t i = 0; i < num_caps * 2; i++) {
    caps[i] = NK_REGION_POS_NONE;
  }

  region->num_caps = num_caps;
  region->caps = caps;
  return NK_SUCCESS;
}

void nk_region_free(nk_region_t* region) {
  if (region == NULL) {
    return;
  }

  free(region->caps);
  region->caps = NULL;
  region->num_caps = 0;
}
