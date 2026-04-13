#include <mruby.h>

#include "mrb_naraku.h"

static mrb_value mrb_naraku_error_message(mrb_state* mrb, mrb_value self) {
  mrb_int err;
  mrb_get_args(mrb, "i", &err);

  const uint8_t* msg = nk_error_message((nk_error_t)err);
  if (msg == NULL) {
    return mrb_nil_value();
  }
  return mrb_str_new_cstr(mrb, (const char*)msg);
}

static mrb_value mrb_naraku_warning_message(mrb_state* mrb, mrb_value self) {
  mrb_int warning;
  mrb_get_args(mrb, "i", &warning);

  const uint8_t* msg = nk_warning_message((nk_warning_t)warning);
  if (msg == NULL) {
    return mrb_nil_value();
  }
  return mrb_str_new_cstr(mrb, (const char*)msg);
}

void mrb_mruby_naraku_gem_init(mrb_state* mrb) {
  struct RClass* naraku_module = mrb_define_module(mrb, "Naraku");

  mrb_define_class_method(mrb, naraku_module, "error_message", mrb_naraku_error_message, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, naraku_module, "warning_message", mrb_naraku_warning_message, MRB_ARGS_REQ(1));

  mrb_define_const(mrb, naraku_module, "FOLD_DEFAULT", mrb_fixnum_value(NK_FOLD_DEFAULT));
  mrb_define_const(mrb, naraku_module, "FOLD_FULL", mrb_fixnum_value(NK_FOLD_FULL));
  mrb_define_const(mrb, naraku_module, "FOLD_TURKISH_AZERI", mrb_fixnum_value(NK_FOLD_TURKISH_AZERI));
  mrb_define_const(mrb, naraku_module, "FOLD_ASCII_ONLY", mrb_fixnum_value(NK_FOLD_ASCII_ONLY));

  mrb_naraku_encoding_gem_init(mrb, naraku_module);
  mrb_naraku_node_gem_init(mrb, naraku_module);
  mrb_naraku_parser_gem_init(mrb, naraku_module);
}

void mrb_mruby_naraku_gem_final(mrb_state* mrb) {
}
