#include <naraku_encoding.h>
#include <naraku_encoding_internal.h>

#include <stdlib.h>  // for malloc, free
#include <string.h>  // for memset

#define CACHE_CHUNK_SIZE 64

void nk_enc_adjust_mbc_head_context_init(nk_adjust_mbc_head_context_t* context, const uint8_t* bytes_begin,
                                         const uint8_t* bytes_end, bool use_cache) {
  context->bytes_begin = bytes_begin;
  context->bytes_end = bytes_end;

  context->cache_start_offset = use_cache ? 0 : NK_DONT_USE_CACHE_FOR_ADJUST_MBC_HEAD;
  context->head_bits_capacity = 0;
  context->head_bits = NULL;
}

void nk_enc_adjust_mbc_head_context_free(nk_adjust_mbc_head_context_t* context) {
  if (context->cache_start_offset == NK_DONT_USE_CACHE_FOR_ADJUST_MBC_HEAD || context->head_bits == NULL) {
    return;
  }

  free(context->head_bits);
  context->head_bits = NULL;
  context->cache_start_offset = 0;
  context->head_bits_capacity = 0;
}

nk_error_t adjust_mbc_head_context_cache_ensure(nk_adjust_mbc_head_context_t* context, size_t target_offset_start,
                                                size_t target_offset_end  // exclusive bound
) {
  if (context->cache_start_offset == NK_DONT_USE_CACHE_FOR_ADJUST_MBC_HEAD ||
      target_offset_start >= target_offset_end) {
    return 0;
  }

  bool needs_expansion = false;
  size_t new_start = context->cache_start_offset;
  size_t new_end = context->cache_start_offset + context->head_bits_capacity * 64;

  if (context->head_bits_capacity == 0) {
    new_start = (target_offset_start / 64) * 64;
    size_t diff = target_offset_end - new_start;
    size_t expand_by = (diff > CACHE_CHUNK_SIZE) ? diff : CACHE_CHUNK_SIZE;
    new_end = ((new_start + expand_by + 63) / 64) * 64;
    needs_expansion = true;
  } else {
    if (target_offset_start < context->cache_start_offset) {
      size_t diff = context->cache_start_offset - target_offset_start;
      size_t expand_by = (diff > CACHE_CHUNK_SIZE) ? diff : CACHE_CHUNK_SIZE;

      if (context->cache_start_offset > expand_by) {
        new_start = ((context->cache_start_offset - expand_by) / 64) * 64;
      } else {
        new_start = 0;
      }
      needs_expansion = true;
    }

    if (target_offset_end > new_end) {
      size_t diff = target_offset_end - new_end;
      size_t expand_by = (diff > CACHE_CHUNK_SIZE) ? diff : CACHE_CHUNK_SIZE;
      new_end = ((new_end + expand_by + 63) / 64) * 64;
      needs_expansion = true;
    }
  }

  if (!needs_expansion) {
    return 0;
  }

  size_t new_capacity = new_end - new_start;
  size_t new_words = new_capacity / 64;
  if (new_words == 0) {
    new_words = 1;
  }

  uint64_t* new_head = (uint64_t*)malloc(new_words * sizeof(uint64_t));
  if (!new_head) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  if (context->head_bits_capacity > 0) {
    size_t old_words = context->head_bits_capacity;
    size_t shift_words = (context->cache_start_offset - new_start) / 64;

    if (shift_words > 0) {
      memset(new_head, 0, shift_words * sizeof(uint64_t));
    }

    memcpy(new_head + shift_words, context->head_bits, old_words * sizeof(uint64_t));

    size_t tail_words = new_words - shift_words - old_words;
    if (tail_words > 0) {
      memset(new_head + shift_words + old_words, 0, tail_words * sizeof(uint64_t));
    }

    free(context->head_bits);
  } else {
    memset(new_head, 0, new_words * sizeof(uint64_t));
  }

  context->head_bits = new_head;
  context->cache_start_offset = new_start;
  context->head_bits_capacity = new_words;

  return 0;
}
