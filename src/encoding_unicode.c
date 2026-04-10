#include <naraku_encoding.h>
#include <naraku_encoding_internal.h>

#if defined(__GNUC__)
#define ARG_UNUSED __attribute__((unused))
#else
#define ARG_UNUSED
#endif

#define FOLD (1 << 0)
#define FOLD_FULL (1 << 1)
#define LOWER (1 << 2)
#define TITLE (1 << 3)
#define UPPER (1 << 4)

#define SPECIAL_FOLD_FULL (1 << 5)
#define SPECIAL_SWAP (1 << 6)
#define SPECIAL_TITLE (1 << 7)
#define SPECIAL_UPPER (1 << 8)
#define SPECIAL_UPPER_TITLE (1 << 9)

#define SET_SPECIAL_INDEX(index) (index << 10)
#define SPECIAL_INDEX(flags) (flags >> 10)

#define UNICODE_SPECIAL_LENGTH_SHIFT 21
#define UNICODE_SPECIAL_CODE_MASK ((1 << UNICODE_SPECIAL_LENGTH_SHIFT) - 1)
#define UNICODE_SPECIAL_LENGTH(code) ((code) >> UNICODE_SPECIAL_LENGTH_SHIFT)
#define UNICODE_SPECIAL_CODE(code) ((code) & UNICODE_SPECIAL_CODE_MASK)

#include ".gen/cprop_range_unicode.gen.h"

struct unicode_fold_item {
  uint32_t from_code;
  uint32_t to_type_flags;
  uint32_t to_codes[NK_ENC_MAX_FOLDED_CODES];
};

struct unicode_case_map_item {
  uint32_t from_code;
  uint32_t type_flags;
  uint32_t to_code;
};

struct unicode_table_entry {
  int16_t name;
  uint16_t index;
};

#define F FOLD
#define FF FOLD_FULL
#define L LOWER
#define T TITLE
#define U UPPER
#define SFF SPECIAL_FOLD_FULL
#define SS SPECIAL_SWAP
#define ST SPECIAL_TITLE
#define SU SPECIAL_UPPER
#define SUT SPECIAL_UPPER_TITLE
#define SI(index) SET_SPECIAL_INDEX(index)

#include ".gen/case_map_unicode.gen.h"

#undef F
#undef FF
#undef L
#undef T
#undef U
#undef SFF
#undef SS
#undef ST
#undef SU
#undef SUT

struct unicode_fold_item* unicode_fold_item_lookup(uint32_t code) {
  uint8_t bytes[3];
  bytes[0] = (code >> 16) & 0xFF;
  bytes[1] = (code >> 8) & 0xFF;
  bytes[2] = code & 0xFF;
  const struct unicode_table_entry* entry = unicode_fold_table_lookup((const char*)bytes, 3);
  if (entry == NULL) {
    return NULL;
  }

  return (struct unicode_fold_item*)(UNICODE_FOLD_ITEMS + entry->index);
}

#define UNICODE_UNFOLD_NOT_FOUND ((uint32_t)(-1))

uint32_t unicode_unfold1_lookup(uint32_t code1) {
  uint8_t bytes[3];
  bytes[0] = (code1 >> 16) & 0xFF;
  bytes[1] = (code1 >> 8) & 0xFF;
  bytes[2] = code1 & 0xFF;
  const struct unicode_table_entry* entry = unicode_unfold1_table_lookup((const char*)bytes, 3);
  if (entry == NULL) {
    return UNICODE_UNFOLD_NOT_FOUND;
  }
  return entry->index;
}

uint32_t unicode_unfold2_lookup(uint32_t code1, uint32_t code2) {
  uint8_t bytes[6];
  bytes[0] = (code1 >> 16) & 0xFF;
  bytes[1] = (code1 >> 8) & 0xFF;
  bytes[2] = code1 & 0xFF;
  bytes[3] = (code2 >> 16) & 0xFF;
  bytes[4] = (code2 >> 8) & 0xFF;
  bytes[5] = code2 & 0xFF;
  const struct unicode_table_entry* entry = unicode_unfold2_table_lookup((const char*)bytes, 6);
  if (entry == NULL) {
    return UNICODE_UNFOLD_NOT_FOUND;
  }
  return entry->index;
}

uint32_t unicode_unfold3_lookup(uint32_t code1, uint32_t code2, uint32_t code3) {
  uint8_t bytes[9];
  bytes[0] = (code1 >> 16) & 0xFF;
  bytes[1] = (code1 >> 8) & 0xFF;
  bytes[2] = code1 & 0xFF;
  bytes[3] = (code2 >> 16) & 0xFF;
  bytes[4] = (code2 >> 8) & 0xFF;
  bytes[5] = code2 & 0xFF;
  bytes[6] = (code3 >> 16) & 0xFF;
  bytes[7] = (code3 >> 8) & 0xFF;
  bytes[8] = code3 & 0xFF;
  const struct unicode_table_entry* entry = unicode_unfold3_table_lookup((const char*)bytes, 9);
  if (entry == NULL) {
    return UNICODE_UNFOLD_NOT_FOUND;
  }
  return entry->index;
}

size_t nk_enc_unicode_get_case_fold(const nk_encoding_t* enc ARG_UNUSED, nk_fold_flag_t flags, uint32_t code,
                                    uint32_t* out_folded_codes) {
  struct unicode_fold_item* item = unicode_fold_item_lookup(code);
  if (item == NULL) {
    out_folded_codes[0] = code;
    return 1;
  }

  uint32_t type_flags = 0;
  if ((flags & NK_FOLD_FULL) != 0) {
    type_flags |= FOLD_FULL;
  } else {
    type_flags |= FOLD;
  }

  if ((flags & NK_FOLD_TURKISH_AZERI) != 0) {
    if (code == 0x0049) {            // LATIN CAPITAL LETTER I
      out_folded_codes[0] = 0x0131;  // LATIN SMALL LETTER DOTLESS I
      return 1;
    }

    if (code == 0x0130) {            // LATIN CAPITAL LETTER I WITH DOT ABOVE
      out_folded_codes[0] = 0x0069;  // LATIN SMALL LETTER I
      return 1;
    }
  }

  if ((type_flags & item->to_type_flags) != 0) {
    for (size_t i = 0; i < NK_ENC_MAX_FOLDED_CODES; i++) {
      if (item->to_codes[i] == 0) {
        return i;
      }
      out_folded_codes[i] = item->to_codes[i];
    }
    return NK_ENC_MAX_FOLDED_CODES;
  }

  if ((type_flags & FOLD_FULL) != 0 && (item->to_type_flags & SPECIAL_FOLD_FULL) != 0) {
    size_t special_index = SPECIAL_INDEX(item->to_type_flags);
    size_t len = UNICODE_SPECIAL_LENGTH(UNICODE_SPECIALS[special_index]);
    for (size_t i = 0; i < len; i++) {
      out_folded_codes[i] = UNICODE_SPECIAL_CODE(UNICODE_SPECIALS[special_index + i]);
    }
    return len;
  }

  out_folded_codes[0] = code;
  return 1;
}

size_t nk_enc_unicode_expand_case_unfold(const nk_encoding_t* enc ARG_UNUSED, nk_fold_flag_t flags,
                                         const uint32_t* folded_codes, size_t folded_codes_len,
                                         nk_unfold_item_t* out_unfold_items) {
  uint32_t type_flags = 0;
  if ((flags & NK_FOLD_FULL) != 0) {
    type_flags |= FOLD_FULL;
  } else {
    type_flags |= FOLD;
  }

  size_t unfold_items_len = 0;

  if ((flags & NK_FOLD_TURKISH_AZERI) != 0) {
    if (folded_codes[0] == 0x0131) {  // LATIN SMALL LETTER DOTLESS I
      out_unfold_items[unfold_items_len].folded_codes_len = 1;
      out_unfold_items[unfold_items_len].unfolded_code = 0x0049;  // LATIN CAPITAL LETTER I
      unfold_items_len++;
    }

    if (folded_codes[0] == 0x0069) {  // LATIN SMALL LETTER I
      out_unfold_items[unfold_items_len].folded_codes_len = 1;
      out_unfold_items[unfold_items_len].unfolded_code = 0x0130;  // LATIN CAPITAL LETTER I WITH DOT ABOVE
      unfold_items_len++;
    }
  }

  uint32_t unfold_index = unicode_unfold1_lookup(folded_codes[0]);
  if (unfold_index != UNICODE_UNFOLD_NOT_FOUND) {
    size_t len = UNICODE_UNFOLD_INDEX_COUNT(UNICODE_UNFOLD_INDICES[unfold_index]);
    for (size_t i = 0; i < len; i++) {
      uint32_t fold_index = UNICODE_UNFOLD_INDEX(UNICODE_UNFOLD_INDICES[unfold_index + i]);
      const struct unicode_fold_item* fold_item = &UNICODE_FOLD_ITEMS[fold_index];
      if ((flags & NK_FOLD_TURKISH_AZERI) != 0 && (fold_item->from_code == 0x0049 || fold_item->from_code == 0x0130)) {
        continue;
      }

      if ((type_flags & fold_item->to_type_flags) != 0) {
        out_unfold_items[unfold_items_len].folded_codes_len = 1;
        out_unfold_items[unfold_items_len].unfolded_code = fold_item->from_code;
        unfold_items_len++;
      }

      if ((type_flags & FOLD_FULL) != 0 && (fold_item->to_type_flags & SPECIAL_FOLD_FULL) != 0) {
        out_unfold_items[unfold_items_len].folded_codes_len = 1;
        out_unfold_items[unfold_items_len].unfolded_code = fold_item->from_code;
        unfold_items_len++;
      }
    }
  }

  if ((type_flags & FOLD_FULL) == 0 || folded_codes_len < 2) {
    return unfold_items_len;
  }

  unfold_index = unicode_unfold2_lookup(folded_codes[0], folded_codes[1]);
  if (unfold_index != UNICODE_UNFOLD_NOT_FOUND) {
    size_t len = UNICODE_UNFOLD_INDEX_COUNT(UNICODE_UNFOLD_INDICES[unfold_index]);
    for (size_t i = 0; i < len; i++) {
      uint32_t fold_index = UNICODE_UNFOLD_INDEX(UNICODE_UNFOLD_INDICES[unfold_index + i]);
      const struct unicode_fold_item* fold_item = &UNICODE_FOLD_ITEMS[fold_index];
      if ((flags & NK_FOLD_TURKISH_AZERI) != 0 && (fold_item->from_code == 0x0049 || fold_item->from_code == 0x0130)) {
        continue;
      }

      if ((type_flags & fold_item->to_type_flags) != 0) {
        out_unfold_items[unfold_items_len].folded_codes_len = 2;
        out_unfold_items[unfold_items_len].unfolded_code = fold_item->from_code;
        unfold_items_len++;
      }

      if ((type_flags & FOLD_FULL) != 0 && (fold_item->to_type_flags & SPECIAL_FOLD_FULL) != 0) {
        out_unfold_items[unfold_items_len].folded_codes_len = 2;
        out_unfold_items[unfold_items_len].unfolded_code = fold_item->from_code;
        unfold_items_len++;
      }
    }
  }

  if (folded_codes_len < 3) {
    return unfold_items_len;
  }

  unfold_index = unicode_unfold3_lookup(folded_codes[0], folded_codes[1], folded_codes[2]);
  if (unfold_index != UNICODE_UNFOLD_NOT_FOUND) {
    size_t len = UNICODE_UNFOLD_INDEX_COUNT(UNICODE_UNFOLD_INDICES[unfold_index]);
    for (size_t i = 0; i < len; i++) {
      uint32_t fold_index = UNICODE_UNFOLD_INDEX(UNICODE_UNFOLD_INDICES[unfold_index + i]);
      const struct unicode_fold_item* fold_item = &UNICODE_FOLD_ITEMS[fold_index];
      if ((flags & NK_FOLD_TURKISH_AZERI) != 0 && (fold_item->from_code == 0x0049 || fold_item->from_code == 0x0130)) {
        continue;
      }

      if ((type_flags & fold_item->to_type_flags) != 0) {
        out_unfold_items[unfold_items_len].folded_codes_len = 3;
        out_unfold_items[unfold_items_len].unfolded_code = fold_item->from_code;
        unfold_items_len++;
      }

      if ((type_flags & FOLD_FULL) != 0 && (fold_item->to_type_flags & SPECIAL_FOLD_FULL) != 0) {
        out_unfold_items[unfold_items_len].folded_codes_len = 3;
        out_unfold_items[unfold_items_len].unfolded_code = fold_item->from_code;
        unfold_items_len++;
      }
    }
  }

  return unfold_items_len;
}

nk_error_t nk_enc_unicode_iterate_case_fold(const nk_encoding_t* enc ARG_UNUSED, nk_fold_flag_t flags,
                                            nk_case_fold_callback_t callback, void* user_data) {
  uint32_t type_flags = 0;
  if ((flags & NK_FOLD_FULL) != 0) {
    type_flags |= FOLD_FULL;
  } else {
    type_flags |= FOLD;
  }

  for (size_t i = 0; i < sizeof(UNICODE_FOLD_ITEMS) / sizeof(struct unicode_fold_item); i++) {
    const struct unicode_fold_item* item = &UNICODE_FOLD_ITEMS[i];

    if (item->from_code == 0x0049 && (flags & NK_FOLD_TURKISH_AZERI) != 0) {  // LATIN CAPITAL LETTER I
      uint32_t folded_code = 0x0131;                                          // LATIN SMALL LETTER DOTLESS I
      nk_error_t err = callback(item->from_code, &folded_code, 1, user_data);
      if (err != 0) {
        return err;
      }
      continue;
    }

    if (item->from_code == 0x0130 && (flags & NK_FOLD_TURKISH_AZERI) != 0) {  // LATIN CAPITAL LETTER I WITH DOT ABOVE
      uint32_t folded_code = 0x0069;                                          // LATIN SMALL LETTER I
      nk_error_t err = callback(item->from_code, &folded_code, 1, user_data);
      if (err != 0) {
        return err;
      }
      continue;
    }

    if ((type_flags & item->to_type_flags) != 0) {
      size_t folded_codes_len = 0;
      for (size_t j = 0; j < NK_ENC_MAX_FOLDED_CODES; j++) {
        if (item->to_codes[j] == 0) {
          break;
        }
        folded_codes_len++;
      }
      nk_error_t err = callback(item->from_code, item->to_codes, folded_codes_len, user_data);
      if (err != 0) {
        return err;
      }
      continue;
    }

    if ((type_flags & FOLD_FULL) != 0 && (item->to_type_flags & SPECIAL_FOLD_FULL) != 0) {
      size_t special_index = SPECIAL_INDEX(item->to_type_flags);
      size_t len = UNICODE_SPECIAL_LENGTH(UNICODE_SPECIALS[special_index]);
      uint32_t folded_codes[NK_ENC_MAX_FOLDED_CODES];
      for (size_t j = 0; j < len; j++) {
        folded_codes[j] = UNICODE_SPECIAL_CODE(UNICODE_SPECIALS[special_index + j]);
      }
      nk_error_t err = callback(item->from_code, folded_codes, len, user_data);
      if (err != 0) {
        return err;
      }
    }
  }

  return NK_SUCCESS;
}

bool nk_enc_unicode_code_is_cprop(const nk_encoding_t* enc ARG_UNUSED, uint32_t code, nk_cprop_t cprop) {
  if (cprop > NK_MAX_CPROP) {
    return false;
  }

  size_t len = (size_t)UNICODE_CPROP_RANGES[cprop][0];
  const uint32_t* intervals = &UNICODE_CPROP_RANGES[cprop][1];
  return code_in_code_range(code, len, intervals);
}

nk_error_t nk_enc_unicode_get_cprop_code_range(const nk_encoding_t* enc ARG_UNUSED, nk_cprop_t cprop,
                                               nk_code_range_delegation_t* out_delegation,
                                               nk_static_code_range_t* out_code_range) {
  if (cprop > NK_MAX_CPROP) {
    return NK_ERR_UNSUPPORTED_CHAR_PROPERTY;
  }

  size_t len = (size_t)UNICODE_CPROP_RANGES[cprop][0];
  const uint32_t* intervals = &UNICODE_CPROP_RANGES[cprop][1];
  out_code_range->len = len;
  out_code_range->intervals = intervals;
  *out_delegation = NK_ENC_NO_DELEGATION;
  return NK_SUCCESS;
}
