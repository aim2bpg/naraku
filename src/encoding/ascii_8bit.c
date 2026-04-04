#include <naraku_encoding.h>

const nk_encoding_t* nk_enc_ascii_8bit = &(const nk_encoding_t){
  .name = "ASCII-8BIT",
  .min_mbc_width = 1,
  .max_mbc_width = 1,
  .single_byte_threshold = 0x100,
  .flags = NK_ENC_FLAG_SELF_SYNC,
  .scan_mbc_width = nk_enc_sb_scan_mbc_width,
  .encode_mbc = nk_enc_sb_encode_mbc,
  .decode_mbc = nk_enc_sb_decode_mbc,
  .adjust_mbc_head = NULL,
  .is_self_sync_string = NULL,
  .get_case_fold = nk_enc_ascii_get_case_fold,
  .expand_case_unfold = nk_enc_ascii_expand_case_unfold,
  .iterate_case_fold = nk_enc_ascii_iterate_case_fold,
  .code_is_ctype = nk_enc_ascii_code_is_ctype,
  .get_ctype_code_range = nk_enc_ascii_get_ctype_code_range,
};
