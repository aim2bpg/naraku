#include <naraku_encoding.h>

static int8_t us_ascii_scan_mbc_width(
  const nk_encoding_t* enc NARAKU_ARG_UNUSED,
  const uint8_t* bytes,
  const uint8_t* bytes_end NARAKU_ARG_UNUSED
) {
  if (*bytes < 128) {
    return 1;
  }

  return 0;
}

static nk_error_t
us_ascii_encode_mbc(const nk_encoding_t* enc NARAKU_ARG_UNUSED, uint32_t code NARAKU_ARG_UNUSED, size_t* out_width NARAKU_ARG_UNUSED, uint8_t* out_bytes NARAKU_ARG_UNUSED) {
  return NK_ERR_CODE_POINT_OUT_OF_RANGE;
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
