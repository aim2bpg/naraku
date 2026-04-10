#include <string.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/hash.h>
#include <mruby/presym.h>
#include <mruby/range.h>
#include <mruby/value.h>

#include "mrb_naraku.h"

// ============================================================================
//
// `Naraku::Encoding` and `Naraku::Encoding::AdjustMbcHeadContext` classes:
//
// ============================================================================

static void mrb_naraku_encoding_free(mrb_state* mrb, void* ptr) {
}
struct mrb_data_type mrb_naraku_encoding_type = {"Encoding", mrb_naraku_encoding_free};

static void mrb_naraku_encoding_adjust_mbc_head_context_free(mrb_state* mrb, void* ptr) {
  nk_adjust_mbc_head_context_t* context = (nk_adjust_mbc_head_context_t*)ptr;
  nk_enc_adjust_mbc_head_context_free(context);
  mrb_free(mrb, (void*)context->bytes_begin);
  mrb_free(mrb, (void*)context);
}
struct mrb_data_type mrb_naraku_encoding_adjust_mbc_head_context_type = {
    "AdjustMbcHeadContext", mrb_naraku_encoding_adjust_mbc_head_context_free};

static const nk_encoding_t* mrb_naraku_encoding_get_ptr(mrb_state* mrb, mrb_value self) {
  return (const nk_encoding_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_encoding_type);
}

static nk_adjust_mbc_head_context_t* mrb_naraku_adjust_mbc_head_context_get_ptr(mrb_state* mrb, mrb_value self) {
  return (nk_adjust_mbc_head_context_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_encoding_adjust_mbc_head_context_type);
}

static mrb_value mrb_naraku_encoding_name_to_cprop(mrb_state* mrb, mrb_value self) {
  char* prop_name;
  mrb_int len;
  mrb_get_args(mrb, "s", &prop_name, &len);

  nk_cprop_t cprop;
  nk_error_t err =
      nk_name_to_cprop(nk_enc_ascii_8bit, (const uint8_t*)prop_name, (const uint8_t*)prop_name + len, &cprop);
  if (err != NK_SUCCESS) {
    switch (err) {
      case NK_ERR_INVALID_CHAR_PROP_NAME:
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid character property name: %l", prop_name, len);
        break;
      default: mrb_raisef(mrb, E_ARGUMENT_ERROR, "unknown error: %d", err); break;
    }
  }

  return mrb_fixnum_value((nk_cprop_t)cprop);
}

// ============================================================================
//
// `Naraku::Encoding` methods:
//
// ============================================================================

static mrb_value mrb_naraku_encoding_new(mrb_state* mrb, struct RClass* encoding_class, const nk_encoding_t* enc) {
  mrb_value obj = mrb_obj_value(mrb_data_object_alloc(mrb, encoding_class, (void*)enc, &mrb_naraku_encoding_type));
  return obj;
}

static mrb_value mrb_naraku_encoding_name(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  const char* name = enc->name;
  if (name == NULL) {
    return mrb_nil_value();
  }
  return mrb_str_new_cstr(mrb, name);
}

static mrb_value mrb_naraku_encoding_min_mbc_width(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  return mrb_fixnum_value(enc->min_mbc_width);
}

static mrb_value mrb_naraku_encoding_max_mbc_width(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  return mrb_fixnum_value(enc->max_mbc_width);
}

static mrb_value mrb_naraku_encoding_single_byte_threshold(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  return mrb_fixnum_value(enc->single_byte_threshold);
}

static mrb_value mrb_naraku_encoding_flags(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  return mrb_fixnum_value(enc->flags);
}

static mrb_value mrb_naraku_encoding_scan_mbc_width(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  char* bytes;
  mrb_int len;
  mrb_get_args(mrb, "s", &bytes, &len);

  const uint8_t* bytes_ptr = (const uint8_t*)bytes;
  const uint8_t* bytes_end = bytes_ptr + len;
  int8_t width = nk_enc_scan_mbc_width(enc, bytes_ptr, bytes_end);
  return mrb_fixnum_value(width);
}

static mrb_value mrb_naraku_encoding_encode_mbc_width(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  mrb_int code;
  mrb_get_args(mrb, "i", &code);

  size_t width;
  nk_error_t err = nk_enc_encode_mbc(enc, (uint32_t)code, &width, NULL);
  if (err != NK_SUCCESS) {
    switch (err) {
      case NK_ERR_INVALID_CODE_POINT: mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid code point: %d", code); break;
      case NK_ERR_TOO_LARGE_CODE_POINT: mrb_raisef(mrb, E_ARGUMENT_ERROR, "too large code point: %d", code); break;
      default: mrb_raisef(mrb, E_ARGUMENT_ERROR, "unknown error: %d", err); break;
    }
  }

  return mrb_fixnum_value((mrb_int)width);
}

static mrb_value mrb_naraku_encoding_encode_mbc(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  mrb_int code;
  mrb_get_args(mrb, "i", &code);

  size_t width;
  uint8_t bytes[NK_ENC_MAX_MBC_WIDTH];
  nk_error_t err = nk_enc_encode_mbc(enc, (uint32_t)code, &width, bytes);
  if (err != NK_SUCCESS) {
    switch (err) {
      case NK_ERR_INVALID_CODE_POINT: mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid code point: %d", code); break;
      case NK_ERR_TOO_LARGE_CODE_POINT: mrb_raisef(mrb, E_ARGUMENT_ERROR, "too large code point: %d", code); break;
      default: mrb_raisef(mrb, E_ARGUMENT_ERROR, "unknown error: %d", err); break;
    }
  }

  return mrb_str_new(mrb, (const char*)bytes, (size_t)width);
}

static mrb_value mrb_naraku_encoding_decode_mbc(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  char* bytes;
  mrb_int len;
  mrb_get_args(mrb, "s", &bytes, &len);

  const uint8_t* bytes_ptr = (const uint8_t*)bytes;
  const uint8_t* bytes_end = bytes_ptr + len;
  uint32_t code = nk_enc_decode_mbc(enc, bytes_ptr, bytes_end);

  return mrb_fixnum_value(code);
}

static mrb_value mrb_naraku_encoding_adjust_mbc_head(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  mrb_int offset;
  void* context_ptr;
  mrb_get_args(mrb, "id", &offset, &context_ptr, &mrb_naraku_encoding_adjust_mbc_head_context_type);

  nk_adjust_mbc_head_context_t* context = (nk_adjust_mbc_head_context_t*)context_ptr;
  const uint8_t* byte_pos = context->bytes_begin + (size_t)offset;
  nk_error_t err = nk_enc_adjust_mbc_head(enc, &byte_pos, context);
  if (err != 0) {
    switch (err) {
      case NK_ERR_MEMORY_ALLOCATION_FAILED:
        mrb_raise(mrb, E_RUNTIME_ERROR, "memory allocation failed during adjust_mbc_head");
        break;
      default: mrb_raisef(mrb, E_RUNTIME_ERROR, "unknown error: %d", err); break;
    }
  }

  return mrb_fixnum_value((mrb_int)(byte_pos - context->bytes_begin));
}

static mrb_value mrb_naraku_encoding_self_sync_string_p(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  char* bytes;
  mrb_int len;
  mrb_get_args(mrb, "s", &bytes, &len);

  const uint8_t* bytes_ptr = (const uint8_t*)bytes;
  const uint8_t* bytes_end = bytes_ptr + len;
  bool is_self_sync = nk_enc_is_self_sync_string(enc, bytes_ptr, bytes_end);
  return is_self_sync ? mrb_true_value() : mrb_false_value();
}

static mrb_value mrb_naraku_encoding_get_case_fold(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  mrb_int code;
  mrb_int flags;
  mrb_get_args(mrb, "ii", &code, &flags);

  uint32_t folded_codes[NK_ENC_MAX_FOLDED_CODES];
  size_t folded_codes_len = nk_enc_get_case_fold(enc, (nk_fold_flag_t)flags, (uint32_t)code, folded_codes);
  if (folded_codes_len == 0) {
    return mrb_nil_value();
  }

  mrb_value ary = mrb_ary_new_capa(mrb, folded_codes_len);
  for (size_t i = 0; i < folded_codes_len; i++) {
    mrb_ary_push(mrb, ary, mrb_fixnum_value(folded_codes[i]));
  }
  return ary;
}

static mrb_value mrb_naraku_encoding_expand_case_unfold(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  mrb_value folded_codes_ary;
  mrb_int flags;
  mrb_get_args(mrb, "Ai", &folded_codes_ary, &flags);

  size_t folded_codes_len = RARRAY_LEN(folded_codes_ary);
  if (folded_codes_len == 0) {
    return mrb_nil_value();
  }

  uint32_t folded_codes[NK_ENC_MAX_FOLDED_CODES];
  for (size_t i = 0; i < folded_codes_len; i++) {
    folded_codes[i] = (uint32_t)mrb_fixnum(mrb_ary_ref(mrb, folded_codes_ary, i));
  }

  nk_unfold_item_t unfold_items[NK_ENC_MAX_UNFOLD_ITEMS];
  size_t unfold_items_len =
      nk_enc_expand_case_unfold(enc, (nk_fold_flag_t)flags, folded_codes, folded_codes_len, unfold_items);
  if (unfold_items_len == 0) {
    return mrb_nil_value();
  }

  mrb_value ary = mrb_ary_new_capa(mrb, unfold_items_len);
  for (size_t i = 0; i < unfold_items_len; i++) {
    const nk_unfold_item_t* item = &unfold_items[i];
    mrb_value item_hash = mrb_hash_new_capa(mrb, 2);
    mrb_hash_set(mrb, item_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "folded_codes_len")),
                 mrb_fixnum_value(item->folded_codes_len));
    mrb_hash_set(mrb, item_hash, mrb_symbol_value(mrb_intern_cstr(mrb, "unfolded_code")),
                 mrb_fixnum_value(item->unfolded_code));
    mrb_ary_push(mrb, ary, item_hash);
  }

  return ary;
}

struct case_fold_callback_data {
  mrb_state* mrb;
  mrb_value callback;
};

static nk_error_t mrb_naraku_encoding_iterate_case_fold_callback(uint32_t code, const uint32_t* folded_codes,
                                                                 size_t folded_codes_len, void* user_data) {
  struct case_fold_callback_data* data = (struct case_fold_callback_data*)user_data;
  mrb_state* mrb = data->mrb;
  mrb_value callback = data->callback;
  mrb_value code_arg = mrb_fixnum_value(code);

  mrb_value folded_codes_ary = mrb_ary_new_capa(mrb, folded_codes_len);
  for (size_t i = 0; i < folded_codes_len; i++) {
    mrb_ary_push(mrb, folded_codes_ary, mrb_fixnum_value(folded_codes[i]));
  }

  (void)mrb_funcall(mrb, callback, "call", 2, code_arg, folded_codes_ary);
  if (mrb->exc) {
    return NK_ERR_INTERNAL_ERROR;
  }

  return 0;
}

static mrb_value mrb_naraku_encoding_iterate_case_fold(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  mrb_int flags;
  mrb_value callback;
  mrb_get_args(mrb, "i&", &flags, &callback);

  struct case_fold_callback_data data = {mrb, callback};
  nk_error_t err =
      nk_enc_iterate_case_fold(enc, (nk_fold_flag_t)flags, mrb_naraku_encoding_iterate_case_fold_callback, &data);

  if (err != 0) {
    switch (err) {
      case NK_ERR_INTERNAL_ERROR: break;
      default: mrb_raisef(mrb, E_RUNTIME_ERROR, "unknown error: %d", err); break;
    }
  }

  return mrb_nil_value();
}

static mrb_value mrb_naraku_encoding_cprop_p(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  mrb_int code;
  mrb_int cprop;
  mrb_get_args(mrb, "ii", &code, &cprop);

  bool result = nk_enc_code_is_cprop(enc, (uint32_t)code, (nk_cprop_t)cprop);
  return result ? mrb_true_value() : mrb_false_value();
}

static mrb_value mrb_naraku_encoding_get_cprop_code_range(mrb_state* mrb, mrb_value self) {
  const nk_encoding_t* enc = mrb_naraku_encoding_get_ptr(mrb, self);
  mrb_int cprop;
  mrb_get_args(mrb, "i", &cprop);

  nk_code_range_delegation_t delegation;
  nk_static_code_range_t code_range;
  nk_error_t err = nk_enc_get_cprop_code_range(enc, (nk_cprop_t)cprop, &delegation, &code_range);
  if (err != NK_SUCCESS) {
    switch (err) {
      case NK_ERR_UNSUPPORTED_CHAR_PROPERTY:
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "unsupported character property: %d", cprop);
        break;
      default: mrb_raisef(mrb, E_ARGUMENT_ERROR, "unknown error: %d", err); break;
    }
  }

  switch (delegation) {
    case NK_ENC_NO_DELEGATION: {
      mrb_value code_range_ary = mrb_ary_new_capa(mrb, code_range.len);
      for (size_t i = 0; i < code_range.len; i++) {
        mrb_value range = mrb_range_new(mrb, mrb_fixnum_value(code_range.intervals[i * 2]),
                                        mrb_fixnum_value(code_range.intervals[i * 2 + 1]), 0);
        mrb_ary_push(mrb, code_range_ary, range);
      }
      return code_range_ary;
    }
    case NK_ENC_7BIT_DELEGATE: return mrb_symbol_value(mrb_intern_cstr(mrb, "delegate_7bit"));
    case NK_ENC_8BIT_DELEGATE: return mrb_symbol_value(mrb_intern_cstr(mrb, "delegate_8bit"));
  }
}

// ============================================================================
//
// `Naraku::Encoding::AdjustMbcHeadContext` methods:
//
// ============================================================================

static mrb_value mrb_naraku_encoding_adjust_mbc_head_context_new(mrb_state* mrb, mrb_value self) {
  char* bytes;
  mrb_int len;
  mrb_bool use_cache;
  mrb_get_args(mrb, "sb", &bytes, &len, &use_cache);

  nk_adjust_mbc_head_context_t* context =
      (nk_adjust_mbc_head_context_t*)mrb_malloc(mrb, sizeof(nk_adjust_mbc_head_context_t));

  char* bytes_copy = (char*)mrb_malloc(mrb, (size_t)len);
  memcpy(bytes_copy, bytes, (size_t)len);
  nk_enc_adjust_mbc_head_context_init(context, (const uint8_t*)bytes_copy, (const uint8_t*)(bytes_copy + len),
                                      use_cache);

  struct RClass* encoding_adjust_mbc_head_context_class = mrb_class_ptr(self);
  return mrb_obj_value(mrb_data_object_alloc(mrb, encoding_adjust_mbc_head_context_class, context,
                                             &mrb_naraku_encoding_adjust_mbc_head_context_type));
}

static mrb_value mrb_naraku_encoding_adjust_mbc_head_context_cache_p(mrb_state* mrb, mrb_value self) {
  nk_adjust_mbc_head_context_t* context = mrb_naraku_adjust_mbc_head_context_get_ptr(mrb, self);
  mrb_int offset;
  mrb_get_args(mrb, "i", &offset);

  return adjust_mbc_head_context_cache_check(context, (size_t)offset) ? mrb_true_value() : mrb_false_value();
}

// ============================================================================
//
// `gem_init` for `Naraku::Encoding` and its related classes:
//
// ============================================================================

void mrb_naraku_encoding_gem_init(mrb_state* mrb, struct RClass* naraku_module) {
  // `Naraku::Encoding`:
  struct RClass* encoding_class = mrb_define_class_under(mrb, naraku_module, "Encoding", mrb->object_class);
  MRB_SET_INSTANCE_TT(encoding_class, MRB_TT_DATA);
  MRB_UNDEF_ALLOCATOR(encoding_class);
  mrb_undef_class_method_id(mrb, encoding_class, MRB_SYM(new));
  mrb_undef_class_method_id(mrb, encoding_class, MRB_SYM(allocate));

  mrb_define_class_method(mrb, encoding_class, "name_to_cprop", mrb_naraku_encoding_name_to_cprop, MRB_ARGS_REQ(1));

  mrb_define_method(mrb, encoding_class, "name", mrb_naraku_encoding_name, MRB_ARGS_NONE());
  mrb_define_method(mrb, encoding_class, "min_mbc_width", mrb_naraku_encoding_min_mbc_width, MRB_ARGS_NONE());
  mrb_define_method(mrb, encoding_class, "max_mbc_width", mrb_naraku_encoding_max_mbc_width, MRB_ARGS_NONE());
  mrb_define_method(mrb, encoding_class, "single_byte_threshold", mrb_naraku_encoding_single_byte_threshold,
                    MRB_ARGS_NONE());
  mrb_define_method(mrb, encoding_class, "flags", mrb_naraku_encoding_flags, MRB_ARGS_NONE());
  mrb_define_method(mrb, encoding_class, "scan_mbc_width", mrb_naraku_encoding_scan_mbc_width, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, encoding_class, "encode_mbc", mrb_naraku_encoding_encode_mbc, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, encoding_class, "encode_mbc_width", mrb_naraku_encoding_encode_mbc_width, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, encoding_class, "decode_mbc", mrb_naraku_encoding_decode_mbc, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, encoding_class, "adjust_mbc_head", mrb_naraku_encoding_adjust_mbc_head, MRB_ARGS_REQ(2));
  mrb_define_method(mrb, encoding_class, "self_sync_string?", mrb_naraku_encoding_self_sync_string_p, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, encoding_class, "_get_case_fold", mrb_naraku_encoding_get_case_fold, MRB_ARGS_REQ(2));
  mrb_define_method(mrb, encoding_class, "_expand_case_unfold", mrb_naraku_encoding_expand_case_unfold,
                    MRB_ARGS_REQ(2));
  mrb_define_method(mrb, encoding_class, "_iterate_case_fold", mrb_naraku_encoding_iterate_case_fold,
                    MRB_ARGS_REQ(1) | MRB_ARGS_BLOCK());
  mrb_define_method(mrb, encoding_class, "_cprop?", mrb_naraku_encoding_cprop_p, MRB_ARGS_REQ(2));
  mrb_define_method(mrb, encoding_class, "_get_cprop_code_range", mrb_naraku_encoding_get_cprop_code_range,
                    MRB_ARGS_REQ(1));

  mrb_define_const(mrb, encoding_class, "ASCII_8BIT", mrb_naraku_encoding_new(mrb, encoding_class, nk_enc_ascii_8bit));
  mrb_define_const(mrb, encoding_class, "ISO_8859_1", mrb_naraku_encoding_new(mrb, encoding_class, nk_enc_iso_8859_1));
  mrb_define_const(mrb, encoding_class, "SHIFT_JIS", mrb_naraku_encoding_new(mrb, encoding_class, nk_enc_shift_jis));
  mrb_define_const(mrb, encoding_class, "US_ASCII", mrb_naraku_encoding_new(mrb, encoding_class, nk_enc_us_ascii));
  mrb_define_const(mrb, encoding_class, "UTF_8", mrb_naraku_encoding_new(mrb, encoding_class, nk_enc_utf_8));

  // `Naraku::Encoding::AdjustMbcHeadContext`:
  struct RClass* encoding_adjust_mbc_head_context_class =
      mrb_define_class_under(mrb, encoding_class, "AdjustMbcHeadContext", mrb->object_class);
  MRB_SET_INSTANCE_TT(encoding_adjust_mbc_head_context_class, MRB_TT_DATA);
  MRB_UNDEF_ALLOCATOR(encoding_adjust_mbc_head_context_class);
  mrb_undef_class_method_id(mrb, encoding_adjust_mbc_head_context_class, MRB_SYM(new));
  mrb_undef_class_method_id(mrb, encoding_adjust_mbc_head_context_class, MRB_SYM(allocate));

  mrb_define_class_method(mrb, encoding_adjust_mbc_head_context_class, "new",
                          mrb_naraku_encoding_adjust_mbc_head_context_new, MRB_ARGS_REQ(2));
  mrb_define_method(mrb, encoding_adjust_mbc_head_context_class, "cache?",
                    mrb_naraku_encoding_adjust_mbc_head_context_cache_p, MRB_ARGS_REQ(1));
}
