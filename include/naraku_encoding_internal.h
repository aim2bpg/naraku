/**
 * @file naraku_encoding_internal.h
 */

#ifndef NARAKU_ENCODING_INTERNAL_H
#define NARAKU_ENCODING_INTERNAL_H

#include <naraku_encoding.h>

#include <string.h>  // for memset

// ==========================================================================
//
// src/adjust_mbc_head_context.c
//
// ==========================================================================

nk_error_t adjust_mbc_head_context_cache_ensure(
  nk_adjust_mbc_head_context_t* context,
  size_t target_offset,
  size_t target_offset_end
);

static inline bool adjust_mbc_head_context_cache_check(nk_adjust_mbc_head_context_t* context, size_t target_offset) {
  if (context->head_bits == NULL || context->cache_start_offset == NK_DONT_USE_CACHE_FOR_ADJUST_MBC_HEAD) {
    return false;
  }

  size_t start = context->cache_start_offset;
  size_t end = context->cache_start_offset + context->head_bits_capacity * 64;
  if (start <= target_offset && target_offset < end) {
    size_t bit_index = (target_offset - start) / 64;
    size_t bit_offset = (target_offset - start) % 64;
    return (context->head_bits[bit_index] & ((uint64_t)1 << bit_offset)) != 0;
  }

  return false;
}

static inline void adjust_mbc_head_context_cache_set(nk_adjust_mbc_head_context_t* context, size_t target_offset) {
  if (context->head_bits == NULL || context->cache_start_offset == NK_DONT_USE_CACHE_FOR_ADJUST_MBC_HEAD) {
    return;
  }

  size_t start = context->cache_start_offset;
  size_t end = context->cache_start_offset + context->head_bits_capacity * 64;
  if (start <= target_offset && target_offset < end) {
    size_t bit_index = (target_offset - start) / 64;
    size_t bit_offset = (target_offset - start) % 64;
    context->head_bits[bit_index] |= ((uint64_t)1 << bit_offset);
  }
}

static inline void adjust_mbc_head_context_cache_fill_step2(
  nk_adjust_mbc_head_context_t* context,
  size_t target_offset,
  size_t target_offset_end
) {
  if (
    context->head_bits == NULL || context->cache_start_offset == NK_DONT_USE_CACHE_FOR_ADJUST_MBC_HEAD ||
    target_offset >= target_offset_end
  ) {
    return;
  }

  size_t start = context->cache_start_offset;
  size_t end = context->cache_start_offset + context->head_bits_capacity * 64;

  if (target_offset < start || target_offset_end > end) {
    return;
  }

  size_t rel_start = target_offset - start;
  size_t rel_end = target_offset_end - start;

  uint64_t pattern = (rel_start % 2 == 0) ? 0x5555555555555555ULL : 0xAAAAAAAAAAAAAAAAULL;

  size_t start_word = rel_start / 64;
  size_t end_word = (rel_end - 1) / 64;

  if (start_word == end_word) {
    int shift_bottom = rel_start % 64;
    int shift_top = 63 - ((rel_end - 1) % 64);

    uint64_t mask = (~0ULL << shift_bottom) & (~0ULL >> shift_top);
    context->head_bits[start_word] |= (pattern & mask);
  } else {
    int shift_bottom = rel_start % 64;
    uint64_t mask_start = ~0ULL << shift_bottom;
    context->head_bits[start_word] |= (pattern & mask_start);

    for (size_t i = start_word + 1; i < end_word; i++) {
      context->head_bits[i] |= pattern;
    }

    int shift_top = 63 - ((rel_end - 1) % 64);
    uint64_t mask_end = ~0ULL >> shift_top;
    context->head_bits[end_word] |= (pattern & mask_end);
  }
}

// ==========================================================================
//
// src/cprop.c
//
// ==========================================================================

/**
 * Returns whether `code` is included in sorted closed intervals:
 * `[range_intervals[0], range_intervals[1]], ...`.
 *
 * Precondition: `range_count >= 1` (i.e., this function is never called with an empty range set).
 */
bool code_in_code_range(uint32_t code, size_t range_count, const uint32_t* range_intervals);

#endif  // NARAKU_ENCODING_INTERNAL_H
