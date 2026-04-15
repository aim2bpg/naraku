#include <naraku_encoding.h>
#include <naraku_encoding_internal.h>

#if defined(__GNUC__)
#define ARG_UNUSED __attribute__((unused))
#else
#define ARG_UNUSED
#endif

#include ".gen/cprop_range_shift_jis.gen.h"
#include ".gen/case_map_shift_jis.gen.h"

static const int8_t SHIFT_JIS_FIRST_BYTE_TABLE[] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0,
};

static const int8_t SHIFT_JIS_SECOND_BYTE_TABLE[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0
};

static int8_t shift_jis_scan_mbc_width(
  const nk_encoding_t* enc ARG_UNUSED,
  const uint8_t* bytes,
  const uint8_t* bytes_end ARG_UNUSED
) {
  int8_t first_byte_width = SHIFT_JIS_FIRST_BYTE_TABLE[*bytes++];
  if (first_byte_width <= 1) return first_byte_width;

  // Now, we assume `first_byte_width == 2`. If `bytes` is not enough for
  // the second byte, we return `-1` to indicate the character is incomplete
  // and one more byte is needed.
  if (bytes == bytes_end) return -1;

  return SHIFT_JIS_SECOND_BYTE_TABLE[*bytes];
}

static nk_error_t
shift_jis_encode_mbc(const nk_encoding_t* enc ARG_UNUSED, uint32_t code, size_t* out_width, uint8_t* out_bytes) {
  if (code > 0xFFFF) {
    return NK_ERR_CODE_POINT_OUT_OF_RANGE;
  }

  if (code & 0xFF00) {
    uint8_t first_byte = (uint8_t)(code >> 8);
    uint8_t second_byte = (uint8_t)(code & 0xFF);
    if (SHIFT_JIS_FIRST_BYTE_TABLE[first_byte] != 2 || SHIFT_JIS_SECOND_BYTE_TABLE[second_byte] != 2) {
      return NK_ERR_INVALID_CODE_POINT;
    }

    if (out_bytes != NULL) {
      out_bytes[0] = first_byte;
      out_bytes[1] = second_byte;
    }

    *out_width = 2;
    return NK_SUCCESS;
  }

  if (SHIFT_JIS_FIRST_BYTE_TABLE[code] != 1) {
    return NK_ERR_INVALID_CODE_POINT;
  }

  if (out_bytes != NULL) {
    *out_bytes = (uint8_t)code;
  }

  *out_width = 1;
  return NK_SUCCESS;
}

static uint32_t
shift_jis_decode_mbc(const nk_encoding_t* enc ARG_UNUSED, const uint8_t* bytes, const uint8_t* bytes_end ARG_UNUSED) {
  uint8_t first_byte = bytes[0];
  int8_t width = SHIFT_JIS_FIRST_BYTE_TABLE[first_byte];
  if (width <= 1) {
    // We assume `width == 1` here because this function should be called only
    // when `bytes` points to the valid byte sequence of a multi-byte
    // character (i.e., `shift_jis_scan_mbc_width` has returned a positive value).
    return first_byte;
  }

  uint32_t code = first_byte;
  code = (code << 8) | bytes[1];
  bytes += 2;
  return code;
}

nk_error_t shift_jis_adjust_mbc_head(
  const nk_encoding_t* enc ARG_UNUSED,
  const uint8_t** bytes_to_adjust,
  nk_adjust_mbc_head_context_t* context
) {
  const uint8_t* target = *bytes_to_adjust;
  const uint8_t* bytes = target;

  if (SHIFT_JIS_SECOND_BYTE_TABLE[*bytes] == 2) {
    while (bytes > context->bytes_begin) {
      bytes--;

      if (adjust_mbc_head_context_cache_check(context, (size_t)(bytes - context->bytes_begin))) {
        break;
      }

      if (SHIFT_JIS_FIRST_BYTE_TABLE[*bytes] != 2) {
        bytes++;
        break;
      }
    }
  }

  size_t target_offset = (size_t)(bytes - context->bytes_begin);
  size_t target_offset_end = (size_t)(target - context->bytes_begin) + 1;
  nk_error_t err = adjust_mbc_head_context_cache_ensure(context, target_offset, target_offset_end);
  if (err < 0) {
    return err;
  }

  adjust_mbc_head_context_cache_set(context, target_offset);

  int8_t width = SHIFT_JIS_FIRST_BYTE_TABLE[*bytes];

  if (bytes + (size_t)width > target) {
    *bytes_to_adjust = bytes;
    return NK_SUCCESS;
  }

  bytes += width;
  adjust_mbc_head_context_cache_fill_step2(context, target_offset + (size_t)width, target_offset_end);

  *bytes_to_adjust = bytes + ((size_t)(target - bytes) & (size_t)~1);
  return NK_SUCCESS;
}

bool shift_jis_is_self_sync_string(
  const nk_encoding_t* enc ARG_UNUSED,
  const uint8_t* bytes,
  const uint8_t* bytes_end ARG_UNUSED
) {
  return SHIFT_JIS_SECOND_BYTE_TABLE[*bytes] != 2;
}

const nk_encoding_t* nk_enc_shift_jis = &(const nk_encoding_t){
  .name = "Shift_JIS",
  .min_mbc_width = 1,
  .max_mbc_width = 2,
  .single_byte_threshold = 0x80,
  .flags = 0,
  .scan_mbc_width = shift_jis_scan_mbc_width,
  .encode_mbc = shift_jis_encode_mbc,
  .decode_mbc = shift_jis_decode_mbc,
  .adjust_mbc_head = shift_jis_adjust_mbc_head,
  .is_self_sync_string = shift_jis_is_self_sync_string,
  .get_case_fold = shift_jis_get_case_fold,
  .expand_case_unfold = shift_jis_expand_case_unfold,
  .iterate_case_fold = shift_jis_iterate_case_fold,
  .code_is_cprop = shift_jis_code_is_cprop,
  .get_cprop_code_range = shift_jis_get_cprop_code_range,
};
