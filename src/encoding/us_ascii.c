#include <naraku_encoding.h>

#if defined(__GNUC__)
#  define ARG_UNUSED __attribute__((unused))
#else
#  define ARG_UNUSED
#endif

static int8_t us_ascii_scan_mbc_width(
    const nk_encoding_t* enc ARG_UNUSED,
    const uint8_t* bytes,
    const uint8_t* bytes_end ARG_UNUSED
) {
  if (*bytes < 128) {
    return 1;
  }

  return 0;
}

static int32_t us_ascii_encode_mbc(
    const nk_encoding_t* enc ARG_UNUSED,
    uint32_t code,
    uint8_t* out_bytes
) {
  if (code < 128) {
    if (out_bytes != NULL) {
      *out_bytes = (uint8_t)code;
    }
    return 1;
  }

  return NK_ERR_TOO_LARGE_CODE_POINT;
}

const nk_encoding_t* nk_enc_us_ascii = &(const nk_encoding_t){
  .name = "US-ASCII",
  .min_mbc_width = 1,
  .max_mbc_width = 1,
  .single_byte_threshold = 0x80,
  .flags = NK_ENC_FLAG_SELF_SYNC,
  .scan_mbc_width = us_ascii_scan_mbc_width,
  .encode_mbc = us_ascii_encode_mbc,
  .decode_mbc = nk_enc_sb_decode_mbc,
  .adjust_mbc_head = NULL,
  .is_self_sync_string = NULL,
  .get_case_fold = nk_enc_ascii_get_case_fold,
  .expand_case_unfold = nk_enc_ascii_expand_case_unfold,
  .iterate_case_fold = nk_enc_ascii_iterate_case_fold,
  .code_is_cprop = nk_enc_ascii_code_is_cprop,
  .get_cprop_code_range = nk_enc_ascii_get_cprop_code_range,
};
