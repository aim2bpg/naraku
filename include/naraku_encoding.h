/**
 * @file naraku_encoding.h
 */

#ifndef NARAKU_ENCODING_H
#define NARAKU_ENCODING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h> // for debug

#include <naraku_common.h>
#include <naraku_error.h>

#include "encoding/ctype_names.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Flag indicating that the encoding is for Unicode code points. */
#define NK_ENC_FLAG_UNICODE (1U << 0)

/**
 * Flag indicating that the encoding supports self-synchronization.
 *
 * This means that the encoding can find a character boundary if a byte stream
 * starts in the middle of a multi-byte character.
 */
#define NK_ENC_FLAG_SELF_SYNC (1U << 1)

/**
 * The maximum width of a multi-byte character in any encoding supported by Naraku.
 */
#define NK_ENC_MAX_MBC_WIDTH 6
// Note: 6 is for CESU-8, which can encode a surrogate pair in 6 bytes.

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

  size_t head_bits_capacity;
  uint64_t* head_bits;
} nk_adjust_mbc_head_context_t;

#define NK_DONT_USE_CACHE_FOR_ADJUST_MBC_HEAD ((size_t)(-1))

/** Flags for case folding behavior. */
typedef enum {
  /** For simple 1-to-1 case folding. */
  NK_FOLD_DEFAULT = 0,
  /** For full case folding (includes 1-to-many mappings like 'ß' to 'ss'). */
  NK_FOLD_FULL = 1 << 0,
  /** For using Turkish/Azeri specific case folding rules (e.g., `İ` to `i`). */
  NK_FOLD_TURKISH_AZERI = 1 << 1,
  /** For ASCII-only case folding. This flag cannot be combined with others. */
  NK_FOLD_ASCII_ONLY = 1 << 2,
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
#define NK_ENC_MAX_UNFOLD_ITEMS 10
// FIXME(makenowjust): This value is tentative. We will compute the actual maximum number.

/**
 * Structure representing a case-unfolded item for a folded code point sequence.
 */
typedef struct {
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
 * Type representing a character type (property).
 */
typedef uint32_t nk_ctype_t;

typedef enum {
  NK_ENC_NO_DELEGATION = 0,
  NK_ENC_7BIT_DELEGATE = 1,
  NK_ENC_8BIT_DELEGATE = 2,

  __nk_code_range_delegation_NK_ERR_UNSUPPORTED_CHAR_PROPERTY = NK_ERR_UNSUPPORTED_CHAR_PROPERTY,
} nk_code_range_delegation_t;

/**
 * Structure representing a code range for character types.
 * 
 * The `codes` pointer points to an array of inversion lists of code points for
 * the character type (i.e., a list of non-overlapping, sorted code point ranges).
 * Each range is represented by a pair of code points (start and end, inclusive).
 */
typedef struct {
  size_t len;
  const uint32_t* intervals;
} nk_static_code_range_t;

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
   * - zero if `bytes` is invalid,
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
   * The function writes the encoded bytes to `out_bytes` and returns the
   * number of bytes written. If `out_bytes` is `NULL`, the function should
   * return the number of bytes that would be written without actually writing
   * anything.
   * 
   * `out_bytes` is guaranteed to have enough space for the maximum width of
   * a multi-byte character in the encoding. Generally, `NK_ENC_MAX_MBC_WIDTH`
   * bytes are sufficient for any encoding, but the function should not write
   * more than `enc->max_mbc_width` bytes.
   *
   * The function should return a negative value if the code point is invalid for the encoding.
   * Such error values are:
   *
   * - `NK_ERR_INVALID_CODE_POINT` if the code point is not valid in the encoding
   *   (e.g., a surrogate code point for UTF-8).
   * - `NK_ERR_TOO_LARGE_CODE_POINT` if the code point is too large to be encoded
   *   in the encoding (e.g., above U+10FFFF for UTF-8).
   */
  int32_t (*encode_mbc)(
    const nk_encoding_t* enc,
    uint32_t code,
    uint8_t* out_bytes
  );

  /**
   * Function pointer to decode a multi-byte character in the encoding.
   * 
   * The function reads bytes from `*bytes_to_decode` up to `bytes_to_decode_end`
   * and returns the decoded cod e point. The function should update `*bytes_to_decode`
   * to point to the byte immediately following the decoded character.
   * 
   * The function assume `*bytes_to_decode < bytes_to_decode_end` holds and
   * `*bytes_to_decode` points to the start of a valid multi-byte character.
   */
  uint32_t (*decode_mbc)(
    const nk_encoding_t* enc,
    const uint8_t** bytes_to_decode,
    const uint8_t* bytes_to_decode_end
  );

  /**
   * Function pointer to adjust the head of a multi-byte character in the encoding.
   *
   * The function takes a pointer to a byte position `*bytes_to_adjust` and adjusts it.
   * 
   * If the encoding does not support self-synchronization, this function should use
   * the provided context to cache the positions of character boundaries to avoid linear
   * scanning of the byte stream.
   * 
   * The function should return `0` on success, or a negative error code if an error occurs. Such error values are:
   * 
   * - `NK_ERR_MEMORY_ALLOCATION_FAILED` if memory allocation for the cache failed.
   */
  nk_error_t (*adjust_mbc_head)(
    const nk_encoding_t* enc,
    const uint8_t** bytes_to_adjust,
    nk_adjust_mbc_head_context_t* context
  );

  /**
   * Function pointer to check if a string is self-synchronizing.
   * 
   * If `flags & NK_ENC_FLAG_SELF_SYNC` is set, this function should return `true`
   * for any valid string in the encoding. If not, it should return `false`
   * if the first byte of the string can be a trail byte of a multi-byte character,
   * and `true` otherwise.
   */
  bool (*is_self_sync_string)(
    const nk_encoding_t* enc,
    const uint8_t* bytes,
    const uint8_t* bytes_end
  );

  /** The name of the encoding (e.g., "UTF-8", "Shift_JIS"). */
  const char* name;

  /** The minimum width of a multi-byte character in the encoding. */
  uint8_t min_mbc_width;

  /** The maximum width of a multi-byte character in the encoding. */
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

  /** Bit flags for the encoding (bitwise OR of `NK_ENC_FLAG_*` constants). */
  uint32_t flags;

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
   * case-folded code points for any code point in the encoding. Generally, `NK_ENC_MAX_FOLDED_CODES`
   * are sufficient for any encoding.
   * 
   * Note that `NK_FOLD_ASCII_ONLY` should not be handled by this function because
   * it is handled by the wrapper function `nk_enc_get_case_fold`.
   */
  size_t (*get_case_fold)(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    uint32_t code,
    uint32_t* folded_codes
  );

  /**
   * Function pointer to get the case-unfolded code points for a given sequence
   * of case-folded code points in the encoding.
   * 
   * The function writes the case-unfolded code points to `unfold_items` and returns
   * the number of code points written.
   * 
   * `unfold_items` is guaranteed to have enough space for the maximum number of
   * case-unfolded code points for any folded code point sequence in the encoding.
   * 
   * This function assumes `folded_codes_len >= 1` and every code point in `folded_codes`
   * is valid for the encoding.
   * 
   * Note that `NK_FOLD_ASCII_ONLY` should not be handled by this function because
   * it is handled by the wrapper function `nk_enc_expand_case_unfold`.
   */
  size_t (*expand_case_unfold)(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    const uint32_t* folded_codes,
    size_t folded_codes_len,
    nk_unfold_item_t* unfold_items
  );

  /**
   * Function pointer to iterate over all case-folded code point sequences for
   * all code points in the encoding.
   * 
   * The function should call the provided `callback` for each case-folded code
   * point sequence, passing the unfolded code point, the folded code points,
   * the number of folded code points, and the `user_data` pointer. If the callback
   * returns a negative value, the iteration should stop and the function should
   * return that value. Otherwise, it should return `0` after iterating over all
   * case-folded code point sequences.
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
   * Function pointer to check if a code point is a character of a certain type in the encoding.
   *
   * Note that this function returns `false` even if:
   *
   * - the code point is invalid for the encoding, or
   * - the character type is not defined for the encoding.
   */
  bool (*code_is_ctype)(
    const nk_encoding_t* enc,
    uint32_t code,
    nk_ctype_t ctype
  );

  /**
   * Function pointer to get the code range for a certain character type in the
   * encoding.
   * 
   * The function writes the code range to `code_range` and returns zero, or a
   * positive value if the character type should be handled by delegation. The
   * delegation values are:
   * 
   * - `NK_ENC_7BIT_DELEGATE` if the character type can be handled by checking
   *   the code point against a 7-bit ASCII range (e.g., the encoding is US-ASCII).
   * - `NK_ENC_8BIT_DELEGATE` if the character type can be handled by checking
   *   the code point against an 8-bit range (e.g., for `ctype` values corresponding
   *   to Latin-1 character classes).
   * 
   * Otherwise, if the function returns a negative value, it means an error occurred:
   * 
   * - `NK_ERR_UNSUPPORTED_CHAR_PROPERTY` if the character type is not supported by the encoding.
   */
  nk_code_range_delegation_t (*get_ctype_code_range)(
    const nk_encoding_t* enc,
    nk_ctype_t ctype,
    nk_static_code_range_t* code_range
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
    uint32_t* folded_codes
);

NARAKU_EXPORTED_FUNCTION
size_t nk_enc_ascii_expand_case_unfold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    const uint32_t* folded_codes,
    size_t folded_codes_len,
    nk_unfold_item_t* unfold_items
);

NARAKU_EXPORTED_FUNCTION
nk_error_t nk_enc_ascii_iterate_case_fold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    nk_case_fold_callback_t callback,
    void* user_data
);

NARAKU_EXPORTED_FUNCTION
bool nk_enc_ascii_code_is_ctype(
    const nk_encoding_t* enc,
    uint32_t code,
    uint32_t ctype
);

NARAKU_EXPORTED_FUNCTION
nk_code_range_delegation_t nk_enc_ascii_get_ctype_code_range(
    const nk_encoding_t* enc,
    uint32_t ctype,
    nk_static_code_range_t* code_range
);

NARAKU_EXPORTED_FUNCTION
nk_code_range_delegation_t nk_enc_ascii_8bit_get_ctype_code_range(
    const nk_encoding_t* enc,
    uint32_t ctype,
    nk_static_code_range_t* code_range
);

NARAKU_EXPORTED_FUNCTION
int8_t nk_enc_sb_scan_mbc_width(
    const nk_encoding_t* enc,
    const uint8_t* bytes,
    const uint8_t* bytes_end
);

NARAKU_EXPORTED_FUNCTION
int32_t nk_enc_sb_encode_mbc(
    const nk_encoding_t* enc,
    uint32_t code,
    uint8_t* out_bytes
);

NARAKU_EXPORTED_FUNCTION
uint32_t nk_enc_sb_decode_mbc(
    const nk_encoding_t* enc,
    const uint8_t** bytes_to_decode,
    const uint8_t* bytes_to_decode_end
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
    uint32_t* folded_codes
);

NARAKU_EXPORTED_FUNCTION
size_t nk_enc_unicode_expand_case_unfold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    const uint32_t* folded_codes,
    size_t folded_codes_len,
    nk_unfold_item_t* unfold_items
);

NARAKU_EXPORTED_FUNCTION
nk_error_t nk_enc_unicode_iterate_case_fold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    nk_case_fold_callback_t callback,
    void* user_data
);

NARAKU_EXPORTED_FUNCTION
bool nk_enc_unicode_code_is_ctype(
    const nk_encoding_t* enc,
    uint32_t code,
    uint32_t ctype
);

NARAKU_EXPORTED_FUNCTION
nk_code_range_delegation_t nk_enc_unicode_get_ctype_code_range(
    const nk_encoding_t* enc,
    uint32_t ctype,
    nk_static_code_range_t* code_range
);

// ==========================================================================
//
// src/ctype.c
//
// ==========================================================================

/**
 * Converts a character type name to the corresponding `nk_ctype_t` value for
 * the given encoding.
 *
 * The function returns the `nk_ctype_t` value if the name is valid for the encoding,
 * or a negative error code if the name is invalid. Such error values are:
 * 
 * - `NK_ERR_INVALID_CHAR_PROPERTY_NAME` if the name is not valid for any encoding.
 * 
 * This function is used for resolving `\p{...}` and `\P{...}` character property escapes.
 */
NARAKU_EXPORTED_FUNCTION
int32_t nk_propname_to_ctype(
  const nk_encoding_t* enc,
  const uint8_t* name_bytes,
  const uint8_t* name_bytes_end
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

static inline int32_t nk_enc_encode_mbc(
    const nk_encoding_t* enc,
    uint32_t code,
    uint8_t* out_bytes
) {
  if (code < enc->single_byte_threshold) {
    if (out_bytes != NULL) {
      *out_bytes = (uint8_t)code;
    }
    return 1;
  }
    
  return enc->encode_mbc(enc, code, out_bytes);
}

static inline uint32_t nk_enc_decode_mbc(
    const nk_encoding_t* enc,
    const uint8_t** bytes_to_decode,
    const uint8_t* bytes_to_decode_end
) {
  if (**bytes_to_decode < enc->single_byte_threshold) {
    return *(*bytes_to_decode)++;
  }

  return enc->decode_mbc(enc, bytes_to_decode, bytes_to_decode_end);
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
    uint32_t* folded_codes
) {
  if ((flags & NK_FOLD_ASCII_ONLY) != 0) {
    return nk_enc_ascii_get_case_fold(enc, flags, code, folded_codes);
  }

  return enc->get_case_fold(enc, flags, code, folded_codes);
}

static inline size_t nk_enc_expand_case_unfold(
    const nk_encoding_t* enc,
    nk_fold_flag_t flags,
    const uint32_t* folded_codes,
    size_t folded_codes_len,
    nk_unfold_item_t* unfold_items
) {
  if ((flags & NK_FOLD_ASCII_ONLY) != 0) {
    return nk_enc_ascii_expand_case_unfold(enc, flags, folded_codes, folded_codes_len, unfold_items);
  }

  return enc->expand_case_unfold(enc, flags, folded_codes, folded_codes_len, unfold_items);
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

static inline bool nk_enc_code_is_ctype(
    const nk_encoding_t* enc,
    uint32_t code,
    nk_ctype_t ctype
) {
  if (ctype == NK_CTYPE_ASCII) {
    return code < 0x80;
  }

  return enc->code_is_ctype(enc, code, ctype);
}

static inline nk_code_range_delegation_t nk_enc_get_ctype_code_range(
    const nk_encoding_t* enc,
    nk_ctype_t ctype,
    nk_static_code_range_t* code_range
) {
  if (ctype == NK_CTYPE_ASCII) {
    return NK_ENC_7BIT_DELEGATE;
  }

  return enc->get_ctype_code_range(enc, ctype, code_range);
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
