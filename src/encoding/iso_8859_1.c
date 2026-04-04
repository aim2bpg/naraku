#include <naraku_encoding.h>

#if defined(__GNUC__)
#  define ARG_UNUSED __attribute__((unused))
#else
#  define ARG_UNUSED
#endif

#include ".gen/ctype_range_iso_8859_1.gen.h"
#include ".gen/case_map_iso_8859_1.gen.h"

const nk_encoding_t* nk_enc_iso_8859_1 = &(const nk_encoding_t){
  .name = "ISO-8859-1",
  .min_mbc_width = 1,
  .max_mbc_width = 1,
  .single_byte_threshold = 0x100,
  .flags = NK_ENC_FLAG_SELF_SYNC,
  .scan_mbc_width = nk_enc_sb_scan_mbc_width,
  .encode_mbc = nk_enc_sb_encode_mbc,
  .decode_mbc = nk_enc_sb_decode_mbc,
  .adjust_mbc_head = NULL,
  .is_self_sync_string = NULL,
  .get_case_fold = iso_8859_1_get_case_fold,
  .expand_case_unfold = iso_8859_1_expand_case_unfold,
  .iterate_case_fold = iso_8859_1_iterate_case_fold,
  .code_is_ctype = iso_8859_1_code_is_ctype,
  .get_ctype_code_range = nk_enc_ascii_8bit_get_ctype_code_range,
};
