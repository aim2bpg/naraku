/**
 * @file naraku_encoding.h
 */

#ifndef NARAKU_ENCODING_H
#define NARAKU_ENCODING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <naraku_common.h>
#include <naraku_error.h>

#include <naraku_cprop_names.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bit flags for encoding properties and behaviors.
 */
typedef enum {

  /**
   * Flag indicating that the encoding is for Unicode code points.
   */
  NK_ENC_FLAG_UNICODE = 1U << 0,

  /**
   * Flag indicating that the encoding supports self-synchronization.
   *
   * This means that the encoding can find a character boundary if a byte stream
   * starts in the middle of a multi-byte character.
   */
  NK_ENC_FLAG_SELF_SYNC = 1U << 1,
} nk_encoding_flag_t;


/**
 * The maximum width of a multi-byte character in any encoding supported by
 * Naraku.
 *
 * The value `6` is for CESU-8.
 */
#define NK_ENC_MAX_MBC_WIDTH 6

/**
 * Context for `nk_enc_adjust_mbc_head`.
 *
 * This structure contains the cache for adjusting the head of a multi-byte
 * character to avoid linear scanning of the byte stream if the encoding does
 * not support self-synchronization.
 */
typedef struct {
  const uint8_t* bytes_begin;
  const uint8_t* bytes_end;

  // If `cache_start_offset` is `NK_DONT_USE_CACHE_FOR_ADJUST_MBC_HEAD`,
  // the cache is not used and the function should perform linear scanning.
  size_t cache_start_offset;

  size_t head_bits_capacity; // in bytes (i.e., number of bits is `head_bits_capacity * 8`)
  uint64_t* head_bits; // nullable
} nk_adjust_mbc_head_context_t;

/**
 * A special value for `cache_start_offset` indicating that the cache should
 * not be used.
 */
#define NK_DONT_USE_CACHE_FOR_ADJUST_MBC_HEAD SIZE_MAX

/**
 * Flags for case folding behavior.
 */
typedef enum {
  /**
   * For simple 1-to-1 character case folding (the default behavior).
   */
  NK_FOLD_DEFAULT = 0,

  /**
   * For full case folding (including 1-to-many characters case folding, such as
   * `'ß'` to `'ss'`).
   */
  NK_FOLD_FULL = (1U << 0),

  /**
   * For Turkish/Azeri specific case folding rules (e.g., `İ` to `i`).
   */
  NK_FOLD_TURKISH_AZERI = (1U << 1),

  /**
   * For ASCII-only case folding. This flag cannot be combined with others.
   */
  NK_FOLD_ASCII_ONLY = (1U << 2),
} nk_fold_flag_t;

/**
 * The maximum number of folded code points for any code point in any encoding
 * supported by Naraku.
 */
#define NK_ENC_MAX_FOLDED_CODES 3

/**
 * The maximum number of items returned by `nk_enc_expand_case_unfold` for any
 * folded code point sequence in any encoding supported by Naraku.
 */
#define NK_ENC_MAX_UNFOLD_ITEMS 5

/**
 * Structure representing a case-unfolded item for a folded code point sequence.
 */
typedef struct {
  // The length of the folded code point sequence corresponding to the unfolded
  // code point. That is, `nk_enc_case_fold(item->unfolded_code, flags, folded_codes)`
  // returns `folded_codes_len` and writes the folded code points
  // (`folded_codes[0], ..., folded_codes[item->folded_codes_len - 1]`) to `folded_codes`.
  size_t folded_codes_len;
  uint32_t unfolded_code;
} nk_unfold_item_t;

/**
 * Callback function type for `nk_enc_iterate_case_fold`.
 */
typedef nk_error_t (*nk_case_fold_callback_t)(
  uint32_t unfolded_code,
  const uint32_t* folded_codes,
  size_t folded_codes_len,
  void* user_data
);

/**
 * Type representing a character property.
 */
typedef uint32_t nk_cprop_t;

/**
 * Type for result of `nk_enc_get_cprop_code_range` indicating how to handle
 * the character property.
 */
typedef enum {
  // The code range for the character property is written to `code_range`,
  // and no delegation is needed.
  NK_ENC_NO_DELEGATION,
  // The character property is delegated to checking the code point against a
  // 7-bit ASCII range (e.g., for US-ASCII).
  NK_ENC_7BIT_DELEGATE,
  // The character property is delegated to checking the code point against an
  // 8-bit range (e.g., for ISO-8859-1 character classes).
  NK_ENC_8BIT_DELEGATE,
} nk_code_range_delegation_t;

/**
 * Structure representing a code range for character properties.
 * 
 * The `codes` pointer points to an array of inversion lists of code points for
 * the character property (i.e., a list of non-overlapping, sorted code point ranges).
 * Each range is represented by a pair of code points (start and end, inclusive).
 */
typedef struct {
  size_t len;
  const uint32_t* intervals;
} nk_static_code_range_t;

/**
 * Structure representing a character encoding and its associated functions
 * for multi-byte character handling, case folding, and character properties.
 */
typedef struct nk_encoding nk_encoding_t;

struct nk_encoding {

  // ==========================================================================
  //
  // Multi-byte character handling functions:
  //
  // ==========================================================================

  /**
   * Function pointer to determine the width of a multi-byte character starting at `bytes`.
   * The function returns:
   *
   * - a positive value of the width in bytes of the multi-byte character if `bytes` is valid and complete,
   * - `0` if `bytes` is invalid,
   * - a negative value of the remaining bytes needed to complete the character if `bytes` is incomplete.
   *
   * The function should not read beyond `bytes_end`, and `bytes < bytes_end` must hold.
   */
  int8_t (*scan_mbc_width)(
    const nk_encoding_t* enc,
    const uint8_t* bytes,
    const uint8_t* bytes_end
  );

  /**
   * Function pointer to encode a code point into a multi-byte character
   * in the encoding.
   *
   * The function writes the width of the encoded multi-byte character to
   * `*out_width` and the bytes of the encoded multi-byte character to
   * `out_bytes`, and returns `NK_SUCCESS` on success. If `out_bytes` is `NULL`,
   * the function should only write the width to `*out_width` and not write
   * to `out_bytes`.
   *
   * `out_bytes` is guaranteed to have enough space for the maximum width of
   * a multi-byte character in the encoding if it is not `NULL`. Generally,
   * `NK_ENC_MAX_MBC_WIDTH` bytes are sufficient for any encoding, but the
   * function should not write more than `enc->max_mbc_width` bytes.
   *
   * The function should return a negative value if the code point is invalid
   * for the encoding. Such error values are:
   *
   * - `NK_ERR_INVALID_CODE_POINT` if the code point is not valid in the encoding
   *   (e.g., a surrogate code point for UTF-8).
   * - `NK_ERR_TOO_LARGE_CODE_POINT` if the code point is too large to be encoded
   *   in the encoding (e.g., above U+10FFFF for UTF-8).
   */
  nk_error_t (*encode_mbc)(
    const nk_encoding_t* enc,
    uint32_t code,
    size_t* out_width,
    uint8_t* out_bytes // nullable
  );

  /**
   * Function pointer to decode a multi-byte character in the encoding.
   * 
   * The function reads bytes from `bytes` up to `bytes_end` and returns the
   * decoded cod e point.
   * 
   * The function assume `bytes < bytes_end` holds and `bytes` points to the
   * start of a valid multi-byte character
   * (i.e., `enc->scan_mbc_width(enc, bytes, bytes_end) == 0`).
   */
  uint32_t (*decode_mbc)(
    const nk_encoding_t* enc,
    const uint8_t* bytes,
    const uint8_t* bytes_end
  );

  /**
   * Function pointer to adjust the head of a multi-byte character in the
   * encoding.
   *
   * The function takes a pointer to a byte position `*bytes_to_adjust` and
   * adjusts it.
   * 
   * If the encoding does not support self-synchronization, this function should
   * use the provided context to cache the positions of character boundaries to
   * avoid linear scanning of the byte stream if `context->cache_start_offset`
   * is not `NK_DONT_USE_CACHE_FOR_ADJUST_MBC_HEAD`.
   *
   * The function assume `*bytes_to_adjust` points to a position in the
   * **valid** multi-byte character sequence, so it does not fail with invalid
   * byte sequences. However, it allocates cache bits, so it may fail with
   * memory allocation (`NK_ERR_MEMORY_ALLOCATION_FAILED`).
   *
   * If `enc->flags & NK_ENC_FLAG_SELF_SYNC` is set and
   * `enc->min_mbc_width == enc->max_mbc_width`, the function should not be
   * called because such encoding can adjust the head without scanning.
   * Therefore, in such case, `enc->adjust_mbc_head` can be `NULL`.
   */
  nk_error_t (*adjust_mbc_head)(
    const nk_encoding_t* enc,
    const uint8_t** bytes_to_adjust,
    nk_adjust_mbc_head_context_t* context
  );

  /**
   * Function pointer to check if a string is self-synchronizing.
   * 
   * If `enc->flags & NK_ENC_FLAG_SELF_SYNC` is set, the function should not
   * be called because such an encoding is self-synchronizing by definition.
   * Therefore, in such case, `enc->is_self_sync_string` can be `NULL`.
   */
  bool (*is_self_sync_string)(
    const nk_encoding_t* enc,
    const uint8_t* bytes,
    const uint8_t* bytes_end
  );

  /**
   * The name of the encoding (e.g., "UTF-8", "Shift_JIS").
   */
  const char* name;

  /**
   * The minimum width of a multi-byte character in the encoding.
   */
  uint8_t min_mbc_width;

  /**
   * The maximum width of a multi-byte character in the encoding.
   */
  uint8_t max_mbc_width;

  /**
   * The threshold for single-byte characters in the encoding.
   *
   * A first byte value below this threshold indicates a single-byte character,
   * while a value at or above it indicates the start of a multi-byte character.
   *
   * For instance:
   *
   * - `0x80` for UTF-8 and Shift_JIS
   * - `0x100` for ASCII-8BIT and ISO-8859-1, and
   * - `0x00` for UTF-16 and UTF-32 (since all characters are multi-byte).
   */
  uint16_t single_byte_threshold;

  /**
   * Bit flags for the encoding (bitwise OR of `NK_ENC_FLAG_*` constants).
   */
  nk_encoding_flag_t flags;

  // ==========================================================================
  //
  // Case folding functions:
  //
  // ==========================================================================

  /**
   * Function pointer to get the case-folded code points for a given code point
   * in the encoding.
   * 
   * The function writes the case-folded code points to `folded_codes` and returns
   * the number of code points written.
   * 
   * `folded_codes` is guaranteed to have enough space for the maximum number of
   * case-folded code points for any code point in the encoding. Generally,
   * `NK_ENC_MAX_FOLDED_CODES` are sufficient for any encoding.
   *
   * The function assume `code` is valid for the encoding, so this function does
   * not fail.
   *
   * Note that `NK_FOLD_ASCII_ONLY` should not be handled by this function because
   * it is handled by the wrapper function `nk_enc_get_case_fold`.
   */
  size_t (*get_case_fold)(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    uint32_t code,
    uint32_t* out_folded_codes
  );

  /**
   * Function pointer to get the case-unfolded code points for a given sequence
   * of case-folded code points in the encoding.
   * 
   * The function writes the case-unfolded code points to `out_unfold_items` and returns
   * the number of code points written.
   * 
   * `unfold_items` is guaranteed to have enough space for the maximum number of
   * case-unfolded code points for any folded code point sequence in the encoding.
   * 
   * This function assumes `folded_codes_len >= 1` and every code point in `folded_codes`
   * is valid and case-folded for the encoding.
   * 
   * Note that `NK_FOLD_ASCII_ONLY` should not be handled by this function because
   * it is handled by the wrapper function `nk_enc_expand_case_unfold`.
   */
  size_t (*expand_case_unfold)(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    const uint32_t* folded_codes,
    size_t folded_codes_len,
    nk_unfold_item_t* out_unfold_items
  );

  /**
   * Function pointer to iterate over all case-folded code point sequences for
   * all code points in the encoding.
   * 
   * The function should call the provided `callback` for each case-folded code
   * point sequence, passing the unfolded code point, the folded code points,
   * the number of folded code points, and the `user_data` pointer. If the callback
   * returns a negative value, the iteration should stop and the function should
   * return that value. Otherwise, it should return `NK_SUCCESS` after iterating
   * over all case-folded code point sequences.
   */
  nk_error_t (*iterate_case_fold)(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    nk_case_fold_callback_t callback,
    void* user_data
  );

  // ==========================================================================
  //
  // Character class functions:
  //
  // ==========================================================================

  /**
   * Function pointer to check if a code point is a character of a certain
   * property in the encoding.
   *
   * Note that this function returns `false` even if:
   *
   * - the code point is valid but does not have the character property, or
   * - the code point is invalid for the encoding, or
   * - the character property is not supported for the encoding.
   */
  bool (*code_is_cprop)(
    const nk_encoding_t* enc,
    uint32_t code,
    nk_cprop_t cprop
  );

  /**
   * Function pointer to get the code range for a certain character property in the
   * encoding.
   * 
   * The function returns `NK_SUCCESS` if the character property is supported and the
   * delegation strategy is written to `*out_delegation`. If `*out_delegation` is
   * `NK_ENC_NO_DELEGATION`, the code range for the character property is written to
   * `out_code_range`. Otherwise, the following delegation strategies are possible:
   * 
   * - `NK_ENC_7BIT_DELEGATE` if the character property can be handled by checking
   *   the code point against a 7-bit ASCII range (e.g., the encoding is US-ASCII).
   * - `NK_ENC_8BIT_DELEGATE` if the character property can be handled by checking
   *   the code point against an 8-bit range (e.g., for `cprop` values corresponding
   *   to Latin-1 character classes).
   * 
   * If the function returns a negative value, it means an error occurred:
   * 
   * - `NK_ERR_UNSUPPORTED_CHAR_PROPERTY` if the character property is not supported
   *   by the encoding.
   */
  nk_error_t (*get_cprop_code_range)(
    const nk_encoding_t* enc,
    nk_cprop_t cprop,
    nk_code_range_delegation_t* out_delegation,
    nk_static_code_range_t* out_code_range
  );
};

// ==========================================================================
//
// src/adjust_mbc_head_context.c
//
// ==========================================================================

void nk_enc_adjust_mbc_head_context_init(
  nk_adjust_mbc_head_context_t* context,
  const uint8_t* bytes_begin,
  const uint8_t* bytes_end,
  bool use_cache
);

void nk_enc_adjust_mbc_head_context_free(nk_adjust_mbc_head_context_t* context);

// ==========================================================================
//
// src/encoding_ascii.c
//
// ==========================================================================

NARAKU_EXPORTED_FUNCTION
size_t nk_enc_ascii_get_case_fold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    uint32_t code,
    uint32_t* out_folded_codes
);

NARAKU_EXPORTED_FUNCTION
size_t nk_enc_ascii_expand_case_unfold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    const uint32_t* folded_codes,
    size_t folded_codes_len,
    nk_unfold_item_t* out_unfold_items
);

NARAKU_EXPORTED_FUNCTION
nk_error_t nk_enc_ascii_iterate_case_fold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    nk_case_fold_callback_t callback,
    void* user_data
);

NARAKU_EXPORTED_FUNCTION
bool nk_enc_ascii_code_is_cprop(
    const nk_encoding_t* enc,
    uint32_t code,
    uint32_t cprop
);

NARAKU_EXPORTED_FUNCTION
nk_error_t nk_enc_ascii_get_cprop_code_range(
    const nk_encoding_t* enc,
    uint32_t cprop,
    nk_code_range_delegation_t* out_delegation,
    nk_static_code_range_t* out_code_range
);

NARAKU_EXPORTED_FUNCTION
nk_error_t nk_enc_ascii_8bit_get_cprop_code_range(
    const nk_encoding_t* enc,
    uint32_t cprop,
    nk_code_range_delegation_t* out_delegation,
    nk_static_code_range_t* out_code_range
);

NARAKU_EXPORTED_FUNCTION
int8_t nk_enc_sb_scan_mbc_width(
    const nk_encoding_t* enc,
    const uint8_t* bytes,
    const uint8_t* bytes_end
);

NARAKU_EXPORTED_FUNCTION
nk_error_t nk_enc_sb_encode_mbc(
    const nk_encoding_t* enc,
    uint32_t code,
    size_t* out_width,
    uint8_t* out_bytes
);

NARAKU_EXPORTED_FUNCTION
uint32_t nk_enc_sb_decode_mbc(
    const nk_encoding_t* enc,
    const uint8_t* bytes,
    const uint8_t* bytes_end
);

// ==========================================================================
//
// src/encoding_unicode.c
//
// ==========================================================================

NARAKU_EXPORTED_FUNCTION
size_t nk_enc_unicode_get_case_fold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    uint32_t code,
    uint32_t* out_folded_codes
);

NARAKU_EXPORTED_FUNCTION
size_t nk_enc_unicode_expand_case_unfold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    const uint32_t* folded_codes,
    size_t folded_codes_len,
    nk_unfold_item_t* out_unfold_items
);

NARAKU_EXPORTED_FUNCTION
nk_error_t nk_enc_unicode_iterate_case_fold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    nk_case_fold_callback_t callback,
    void* user_data
);

NARAKU_EXPORTED_FUNCTION
bool nk_enc_unicode_code_is_cprop(
    const nk_encoding_t* enc,
    uint32_t code,
    uint32_t cprop
);

NARAKU_EXPORTED_FUNCTION
nk_error_t nk_enc_unicode_get_cprop_code_range(
    const nk_encoding_t* enc,
    uint32_t cprop,
    nk_code_range_delegation_t* out_delegation,
    nk_static_code_range_t* out_code_range
);

// ==========================================================================
//
// src/cprop.c
//
// ==========================================================================

/**
 * Converts a character type name to the corresponding `nk_cprop_t` value for
 * the given encoding.
 * 
 * If the name is valid, the function writes the corresponding `nk_cprop_t`
 * value to `*out_cprop` and returns `NK_SUCCESS`. Otherwise, it returns a
 * negative error code. Such error values are:
 * 
 * - `NK_ERR_INVALID_CHAR_PROPERTY_NAME` if the name is not valid for any encoding.
 * 
 * This function is used for resolving `\p{...}` and `\P{...}` character property
 * escapes.
 */
NARAKU_EXPORTED_FUNCTION
nk_error_t nk_name_to_cprop(
  const nk_encoding_t* enc,
  const uint8_t* name_bytes,
  const uint8_t* name_bytes_end,
  nk_cprop_t* out_cprop
);

// ==========================================================================
//
// Wrapper functions:
//
// ==========================================================================

static inline int8_t nk_enc_scan_mbc_width(
    const nk_encoding_t* enc,
    const uint8_t* bytes,
    const uint8_t* bytes_end
) {
  if (enc->min_mbc_width == enc->max_mbc_width && (enc->single_byte_threshold & 0xFF) == 0) {
    return (int8_t)enc->min_mbc_width;
  }

  return enc->scan_mbc_width(enc, bytes, bytes_end);
}

static inline nk_error_t nk_enc_encode_mbc(
    const nk_encoding_t* enc,
    uint32_t code,
    size_t* out_width,
    uint8_t* out_bytes
) {
  if (code < enc->single_byte_threshold) {
    *out_width = 1;
    if (out_bytes != NULL) {
      *out_bytes = (uint8_t)code;
    }
    return NK_SUCCESS;
  }
    
  return enc->encode_mbc(enc, code, out_width, out_bytes);
}

static inline uint32_t nk_enc_decode_mbc(
    const nk_encoding_t* enc,
    const uint8_t* bytes,
    const uint8_t* bytes_end
) {
  if (*bytes < enc->single_byte_threshold) {
    return *bytes;
  }

  return enc->decode_mbc(enc, bytes, bytes_end);
}

static inline nk_error_t nk_enc_adjust_mbc_head(
    const nk_encoding_t* enc,
    const uint8_t** bytes_to_adjust,
    nk_adjust_mbc_head_context_t* context
) {
  if (enc->min_mbc_width == enc->max_mbc_width) {
    if (enc->min_mbc_width == 1) {
      return 0;
    }

    size_t offset = (size_t)(*bytes_to_adjust - context->bytes_begin);
    size_t remainder = offset % enc->min_mbc_width;
    if (remainder != 0) {
      *bytes_to_adjust -= remainder;
    }
    return 0;
  }

  if (*bytes_to_adjust >= context->bytes_end) {
    return 0;
  }

  if ((enc->flags & NK_ENC_FLAG_SELF_SYNC) == 0 && context->head_bits != NULL && context->cache_start_offset != NK_DONT_USE_CACHE_FOR_ADJUST_MBC_HEAD) {
    size_t offset = (size_t)(*bytes_to_adjust - context->bytes_begin);
    if (context->cache_start_offset <= offset) {
      for (size_t i = 0; i < enc->max_mbc_width && offset - i >= context->cache_start_offset; i++) {
        size_t bit_index = (offset - i) / 64;
        size_t bit_offset = (offset - i) % 64;
        if (bit_index < context->head_bits_capacity && (context->head_bits[bit_index] & ((uint64_t)1 << bit_offset)) != 0) {
          const uint8_t* candidate = *bytes_to_adjust - i;
          int8_t width = nk_enc_scan_mbc_width(enc, candidate, context->bytes_end);
          if (width > 0 && candidate + width > *bytes_to_adjust) {
            *bytes_to_adjust = candidate;
            return 0;
          }
        }
      }
    }
  }

  return enc->adjust_mbc_head(enc, bytes_to_adjust, context);
}

static inline bool nk_enc_is_self_sync_string(
    const nk_encoding_t* enc,
    const uint8_t* bytes,
    const uint8_t* bytes_end
) {
  if ((enc->flags & NK_ENC_FLAG_SELF_SYNC) != 0) {
    return true;
  }

  return enc->is_self_sync_string(enc, bytes, bytes_end);
}

static inline size_t nk_enc_get_case_fold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    uint32_t code,
    uint32_t* out_folded_codes
) {
  if ((flags & NK_FOLD_ASCII_ONLY) != 0) {
    return nk_enc_ascii_get_case_fold(enc, flags, code, out_folded_codes);
  }

  return enc->get_case_fold(enc, flags, code, out_folded_codes);
}

static inline size_t nk_enc_expand_case_unfold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    const uint32_t* folded_codes,
    size_t folded_codes_len,
    nk_unfold_item_t* out_unfold_items
) {
  if ((flags & NK_FOLD_ASCII_ONLY) != 0) {
    return nk_enc_ascii_expand_case_unfold(enc, flags, folded_codes, folded_codes_len, out_unfold_items);
  }

  return enc->expand_case_unfold(enc, flags, folded_codes, folded_codes_len, out_unfold_items);
}

static inline nk_error_t nk_enc_iterate_case_fold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    nk_case_fold_callback_t callback,
    void* user_data
) {
  if ((flags & NK_FOLD_ASCII_ONLY) != 0) {
    return nk_enc_ascii_iterate_case_fold(enc, flags, callback, user_data);
  }

  return enc->iterate_case_fold(enc, flags, callback, user_data);
}

static inline bool nk_enc_code_is_cprop(
    const nk_encoding_t* enc,
    uint32_t code,
    nk_cprop_t cprop
) {
  if (cprop == NK_CPROP_ASCII) {
    return code < 0x80;
  }

  return enc->code_is_cprop(enc, code, cprop);
}

static inline nk_error_t nk_enc_get_cprop_code_range(
    const nk_encoding_t* enc,
    nk_cprop_t cprop,
    nk_code_range_delegation_t* out_delegation,
    nk_static_code_range_t* out_code_range
) {
  if (cprop == NK_CPROP_ASCII) {
    *out_delegation = NK_ENC_7BIT_DELEGATE;
    return NK_SUCCESS;
  }

  return enc->get_cprop_code_range(enc, cprop, out_delegation, out_code_range);
}

// ==========================================================================
//
// Supported encodings:
//
// ==========================================================================

NARAKU_EXPORTED_DATA
const nk_encoding_t* nk_enc_ascii_8bit;

NARAKU_EXPORTED_DATA
const nk_encoding_t* nk_enc_iso_8859_1;

NARAKU_EXPORTED_DATA
const nk_encoding_t* nk_enc_shift_jis;

NARAKU_EXPORTED_DATA
const nk_encoding_t* nk_enc_us_ascii;

NARAKU_EXPORTED_DATA
const nk_encoding_t* nk_enc_utf_8;

#ifdef __cplusplus
}
#endif

#endif // NARAKU_ENCODING_H
