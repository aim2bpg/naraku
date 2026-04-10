#include <naraku_encoding.h>

#if defined(__GNUC__)
#define ARG_UNUSED __attribute__((unused))
#else
#define ARG_UNUSED
#endif

#include ".gen/cprop_range_ascii.gen.h"
#include ".gen/case_map_ascii.gen.h"

nk_error_t nk_enc_ascii_get_cprop_code_range(
  const nk_encoding_t* enc ARG_UNUSED,
  uint32_t cprop,
  nk_code_range_delegation_t* out_delegation,
  nk_static_code_range_t* out_code_range ARG_UNUSED
) {
  if (cprop <= NK_MAX_DEFAULT_SUPPORT_CPROP) {
    *out_delegation = NK_ENC_7BIT_DELEGATE;
    return NK_SUCCESS;
  }

  return NK_ERR_UNSUPPORTED_CHAR_PROPERTY;
}

nk_error_t nk_enc_ascii_8bit_get_cprop_code_range(
  const nk_encoding_t* enc ARG_UNUSED,
  uint32_t cprop,
  nk_code_range_delegation_t* out_delegation,
  nk_static_code_range_t* out_code_range ARG_UNUSED
) {
  if (cprop <= NK_MAX_DEFAULT_SUPPORT_CPROP) {
    *out_delegation = NK_ENC_8BIT_DELEGATE;
    return NK_SUCCESS;
  }

  return NK_ERR_UNSUPPORTED_CHAR_PROPERTY;
}

int8_t nk_enc_sb_scan_mbc_width(
  const nk_encoding_t* enc ARG_UNUSED,
  const uint8_t* bytes ARG_UNUSED,
  const uint8_t* bytes_end ARG_UNUSED
) {
  return 1;
}

nk_error_t
nk_enc_sb_encode_mbc(const nk_encoding_t* enc ARG_UNUSED, uint32_t code, size_t* out_width, uint8_t* out_bytes) {
  if (code < 256) {
    *out_width = 1;
    if (out_bytes != NULL) {
      *out_bytes = (uint8_t)code;
    }
    return NK_SUCCESS;
  }

  return NK_ERR_TOO_LARGE_CODE_POINT;
}

uint32_t
nk_enc_sb_decode_mbc(const nk_encoding_t* enc ARG_UNUSED, const uint8_t* bytes, const uint8_t* bytes_end ARG_UNUSED) {
  return bytes[0];
}
