#include <naraku_encoding.h>

#if defined(__GNUC__)
#define ARG_UNUSED __attribute__((unused))
#else
#define ARG_UNUSED
#endif

static const int8_t UTF_8_EXPECTED_WIDTH[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

#define UTF_8_TRANS_ACCEPT (-1)
#define UTF_8_TRANS_FAILURE (-2)

#define A UTF_8_TRANS_ACCEPT
#define F UTF_8_TRANS_FAILURE

static const int8_t UTF_8_TRANS_TABLE[][0x100] = {
    {/* S0   0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
     /* 0 */ A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,
     /* 1 */ A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,
     /* 2 */ A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,
     /* 3 */ A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,
     /* 4 */ A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,
     /* 5 */ A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,
     /* 6 */ A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,
     /* 7 */ A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,
     /* 8 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 9 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* a */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* b */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* c */ F, F, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     /* d */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     /* e */ 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 3, 3,
     /* f */ 5, 6, 6, 6, 7, F, F, F, F, F, F, F, F, F, F, F},
    {/* S1   0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
     /* 0 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 1 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 2 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 3 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 4 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 5 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 6 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 7 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 8 */ A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,
     /* 9 */ A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,
     /* a */ A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,
     /* b */ A, A, A, A, A, A, A, A, A, A, A, A, A, A, A, A,
     /* c */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* d */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* e */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* f */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F},
    {/* S2   0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
     /* 0 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 1 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 2 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 3 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 4 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 5 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 6 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 7 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 8 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 9 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* a */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     /* b */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     /* c */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* d */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* e */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* f */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F},
    {/* S3   0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
     /* 0 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 1 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 2 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 3 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 4 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 5 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 6 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 7 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 8 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     /* 9 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     /* a */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     /* b */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     /* c */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* d */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* e */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* f */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F},
    {/* S4   0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
     /* 0 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 1 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 2 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 3 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 4 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 5 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 6 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 7 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 8 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     /* 9 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     /* a */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* b */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* c */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* d */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* e */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* f */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F},
    {/* S5   0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
     /* 0 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 1 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 2 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 3 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 4 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 5 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 6 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 7 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 8 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 9 */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
     /* a */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
     /* b */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
     /* c */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* d */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* e */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* f */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F},
    {/* S6   0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
     /* 0 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 1 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 2 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 3 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 4 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 5 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 6 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 7 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 8 */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
     /* 9 */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
     /* a */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
     /* b */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
     /* c */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* d */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* e */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* f */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F},
    {/* S7   0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
     /* 0 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 1 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 2 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 3 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 4 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 5 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 6 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 7 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* 8 */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
     /* 9 */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* a */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* b */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* c */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* d */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* e */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F,
     /* f */ F, F, F, F, F, F, F, F, F, F, F, F, F, F, F, F},
};
#undef A
#undef F

static int8_t utf_8_scan_mbc_width(const nk_encoding_t* enc ARG_UNUSED, const uint8_t* bytes,
                                   const uint8_t* bytes_end) {
  int8_t expected_width = UTF_8_EXPECTED_WIDTH[*bytes];
  int8_t state = UTF_8_TRANS_TABLE[0][*bytes++];
  if (state < 0) {
    return state == UTF_8_TRANS_FAILURE ? 0 : 1;
  }
  if (bytes == bytes_end) {
    return 1 - expected_width;
  }

  state = UTF_8_TRANS_TABLE[state][*bytes++];
  if (state < 0) {
    return state == UTF_8_TRANS_FAILURE ? 0 : 2;
  }
  if (bytes == bytes_end) {
    return 2 - expected_width;
  }

  state = UTF_8_TRANS_TABLE[state][*bytes++];
  if (state < 0) {
    return state == UTF_8_TRANS_FAILURE ? 0 : 3;
  }
  if (bytes == bytes_end) {
    return 3 - expected_width;
  }

  state = UTF_8_TRANS_TABLE[state][*bytes++];
  return state == UTF_8_TRANS_FAILURE ? 0 : 4;
}

static nk_error_t utf_8_encode_mbc(const nk_encoding_t* enc ARG_UNUSED, uint32_t code, size_t* out_width,
                                   uint8_t* out_bytes) {
  int32_t mbc_len = 0;
  if (code <= 0x7F) {
    mbc_len = 1;
  } else if (code <= 0x7FF) {
    mbc_len = 2;
  } else if (code <= 0xFFFF) {
    // check for surrogate code points
    if (code >= 0xD800 && code <= 0xDFFF) {
      return NK_ERR_INVALID_CODE_POINT;
    }
    mbc_len = 3;
  } else if (code <= 0x10FFFF) {
    mbc_len = 4;
  } else {
    return NK_ERR_TOO_LARGE_CODE_POINT;
  }

  *out_width = (size_t)mbc_len;
  if (out_bytes == NULL) {
    return NK_SUCCESS;
  }

  switch (mbc_len) {
    case 1: out_bytes[0] = (uint8_t)code; break;
    case 2:
      out_bytes[0] = (uint8_t)(0xC0 | (code >> 6));
      out_bytes[1] = (uint8_t)(0x80 | (code & 0x3F));
      break;
    case 3:
      out_bytes[0] = (uint8_t)(0xE0 | (code >> 12));
      out_bytes[1] = (uint8_t)(0x80 | ((code >> 6) & 0x3F));
      out_bytes[2] = (uint8_t)(0x80 | (code & 0x3F));
      break;
    case 4:
      out_bytes[0] = (uint8_t)(0xF0 | (code >> 18));
      out_bytes[1] = (uint8_t)(0x80 | ((code >> 12) & 0x3F));
      out_bytes[2] = (uint8_t)(0x80 | ((code >> 6) & 0x3F));
      out_bytes[3] = (uint8_t)(0x80 | (code & 0x3F));
      break;
  }

  return NK_SUCCESS;
}

static uint32_t utf_8_decode_mbc(const nk_encoding_t* enc ARG_UNUSED, const uint8_t* bytes,
                                 const uint8_t* bytes_end ARG_UNUSED) {
  int8_t expected_width = UTF_8_EXPECTED_WIDTH[bytes[0]];
  switch (expected_width) {
    case 2: return (uint32_t)(((bytes[0] & 0x1F) << 6) | ((bytes[1] & 0x3F)));
    case 3: return (uint32_t)(((bytes[0] & 0x0F) << 12) | (((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F)));
    case 4:
      return (uint32_t)(((bytes[0] & 0x07) << 18) |
                        (((bytes[1] & 0x3F) << 12) | (((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F))));
    default: return (uint32_t)bytes[0];
  }
}

nk_error_t utf_8_adjust_mbc_head(const nk_encoding_t* enc ARG_UNUSED, const uint8_t** bytes_to_adjust,
                                 nk_adjust_mbc_head_context_t* context) {
  while (context->bytes_begin < *bytes_to_adjust && (**bytes_to_adjust & 0xC0) == 0x80) {
    (*bytes_to_adjust)--;
  }

  return NK_SUCCESS;
}

const nk_encoding_t* nk_enc_utf_8 = &(nk_encoding_t){
    .name = "UTF-8",
    .min_mbc_width = 1,
    .max_mbc_width = 4,
    .single_byte_threshold = 0x80,
    .flags = NK_ENC_FLAG_SELF_SYNC | NK_ENC_FLAG_UNICODE,
    .scan_mbc_width = utf_8_scan_mbc_width,
    .encode_mbc = utf_8_encode_mbc,
    .decode_mbc = utf_8_decode_mbc,
    .adjust_mbc_head = utf_8_adjust_mbc_head,
    .is_self_sync_string = NULL,
    .get_case_fold = nk_enc_unicode_get_case_fold,
    .expand_case_unfold = nk_enc_unicode_expand_case_unfold,
    .iterate_case_fold = nk_enc_unicode_iterate_case_fold,
    .code_is_cprop = nk_enc_unicode_code_is_cprop,
    .get_cprop_code_range = nk_enc_unicode_get_cprop_code_range,
};
