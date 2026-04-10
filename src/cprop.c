#include <naraku_encoding.h>

#if defined(__GNUC__)
#define ARG_UNUSED __attribute__((unused))
#else
#define ARG_UNUSED
#endif

#include ".gen/name2cprop.gen.h"

#include <stdio.h>

nk_error_t nk_name_to_cprop(const nk_encoding_t* enc, const uint8_t* name_bytes, const uint8_t* name_bytes_end,
                            nk_cprop_t* out_cprop) {
  uint8_t ascii_bytes[CPROP_NAME_MAX_BYTES];
  size_t ascii_bytes_len = 0;

  for (uint8_t* p = (uint8_t*)name_bytes; p < name_bytes_end;) {
    int8_t width = nk_enc_scan_mbc_width(enc, p, name_bytes_end);
    if (width <= 0) {
      return NK_ERR_INVALID_CHAR_PROP_NAME;
    }

    uint32_t code = nk_enc_decode_mbc(enc, p, name_bytes_end);
    if (code >= 0x80) {
      return NK_ERR_INVALID_CHAR_PROP_NAME;
    }
    p += width;

    if (code == '_' || code == '-' || code == ' ') {
      continue;
    }

    uint32_t folded_code;
    nk_enc_ascii_get_case_fold(enc, NK_FOLD_ASCII_ONLY, code, &folded_code);
    if (ascii_bytes_len >= CPROP_NAME_MAX_BYTES) {
      return NK_ERR_INVALID_CHAR_PROP_NAME;
    }

    ascii_bytes[ascii_bytes_len++] = (uint8_t)folded_code;
  }

  const struct name2cprop_entry* entry = name2cprop_lookup((const char*)ascii_bytes, (unsigned int)ascii_bytes_len);
  if (entry == NULL) {
    return NK_ERR_INVALID_CHAR_PROP_NAME;
  }

  *out_cprop = (nk_cprop_t)entry->cprop;
  return NK_SUCCESS;
}

bool code_in_code_range(uint32_t code, size_t range_count, const uint32_t* range_intervals) {
  size_t left = 0, right = range_count - 1;
  while (left <= right) {
    size_t mid = left + (right - left) / 2;
    uint32_t range_start = range_intervals[mid * 2];
    uint32_t range_end = range_intervals[mid * 2 + 1];
    if (code < range_start) {
      right = mid - 1;
    } else if (code > range_end) {
      left = mid + 1;
    } else {
      return true;
    }
  }
  return false;
}
