#include <naraku_encoding.h>

#if defined(__GNUC__)
#  define ARG_UNUSED __attribute__((unused))
#else
#  define ARG_UNUSED
#endif

#include ".gen/cprop_range_ascii.gen.h"
#include ".gen/case_map_ascii.gen.h"

nk_code_range_delegation_t nk_enc_ascii_get_cprop_code_range(
    const nk_encoding_t* enc ARG_UNUSED,
    uint32_t cprop,
    nk_static_code_range_t* code_range ARG_UNUSED
) {
  if (cprop <= NK_MAX_DEFAULT_SUPPORT_CPROP) {
    return NK_ENC_7BIT_DELEGATE;
  }

  return NK_ERR_UNSUPPORTED_CHAR_PROPERTY;
}

nk_code_range_delegation_t nk_enc_ascii_8bit_get_cprop_code_range(
    const nk_encoding_t* enc ARG_UNUSED,
    uint32_t cprop,
    nk_static_code_range_t* code_range ARG_UNUSED
) {
  if (cprop <= NK_MAX_DEFAULT_SUPPORT_CPROP) {
    return NK_ENC_8BIT_DELEGATE;
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

int32_t nk_enc_sb_encode_mbc(
    const nk_encoding_t* enc ARG_UNUSED,
    uint32_t code,
    uint8_t* out_bytes
) {
  if (code < 256) {
    if (out_bytes != NULL) {
      *out_bytes = (uint8_t)code;
    }
    return 1;
  }

  return NK_ERR_TOO_LARGE_CODE_POINT;
}

uint32_t nk_enc_sb_decode_mbc(
    const nk_encoding_t* enc ARG_UNUSED,
    const uint8_t** bytes_to_decode,
    const uint8_t* bytes_to_decode_end ARG_UNUSED
) {
  return *(*bytes_to_decode)++;
}
