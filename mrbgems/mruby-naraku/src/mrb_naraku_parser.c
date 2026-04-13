#include <string.h>
#include <stdlib.h>

#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/presym.h>
#include <mruby/value.h>
#include <mruby/variable.h>

#include "mrb_naraku.h"

// ============================================================================
//
// `Naraku::Parser` class:
//
// ============================================================================

typedef struct {
  mrb_state* mrb;
  mrb_value warning_func;
} mrb_naraku_warning_context_t;

static void mrb_naraku_warning_callback(const nk_parser_t* parser, nk_warning_t warning, size_t offset, size_t length) {
  mrb_naraku_warning_context_t* context = (mrb_naraku_warning_context_t*)parser->user_data;
  if (context == NULL) {
    return;
  }

  mrb_funcall(
    context->mrb,
    context->warning_func,
    "call",
    3,
    mrb_fixnum_value((mrb_int)warning),
    mrb_fixnum_value((mrb_int)offset),
    mrb_fixnum_value((mrb_int)length)
  );
}

static void mrb_naraku_parser_free(mrb_state* mrb, void* ptr) {
  nk_parser_t* parser = (nk_parser_t*)ptr;
  if (parser->user_data != NULL) {
    mrb_free(mrb, parser->user_data);
  }
  mrb_free(mrb, (void*)parser->pattern_bytes_begin);
  nk_parser_free(parser);
}
struct mrb_data_type mrb_naraku_parser_type = {"Parser", mrb_naraku_parser_free};

static nk_parser_t* mrb_naraku_parser_get_ptr(mrb_state* mrb, mrb_value self) {
  return (nk_parser_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_parser_type);
}

static mrb_value mrb_naraku_parser_new(mrb_state* mrb, mrb_value self) {
  void* encoding_ptr;

  char* pattern;
  mrb_int pattern_len;

  mrb_bool is_extended_mode;
  mrb_bool is_ignore_case;
  mrb_bool dot_allows_newline;
  mrb_bool char_class_is_strict;
  mrb_bool char_type_is_ascii_only;
  mrb_bool posix_char_class_is_ascii_only;
  mrb_int fold_flags;
  mrb_int range_quantifier_max_repetition;
  mrb_int bare_back_ref_max_num;
  mrb_int max_group_num;
  mrb_int back_ref_max_num;
  mrb_int max_capture_depth;
  mrb_int max_parse_depth;
  mrb_value warning_func;

  mrb_get_args(
    mrb,
    "dsbbbbbbiiiiiiio",
    &encoding_ptr,
    &mrb_naraku_encoding_type,
    &pattern,
    &pattern_len,
    &is_extended_mode,
    &is_ignore_case,
    &dot_allows_newline,
    &char_class_is_strict,
    &char_type_is_ascii_only,
    &posix_char_class_is_ascii_only,
    &fold_flags,
    &range_quantifier_max_repetition,
    &bare_back_ref_max_num,
    &max_group_num,
    &back_ref_max_num,
    &max_capture_depth,
    &max_parse_depth,
    &warning_func
  );

  const nk_encoding_t* enc = (const nk_encoding_t*)encoding_ptr;

  nk_parser_t* parser = mrb_malloc(mrb, sizeof(nk_parser_t));
  uint8_t* pattern_copy = (uint8_t*)mrb_malloc(mrb, (size_t)pattern_len);
  memcpy(pattern_copy, pattern, (size_t)pattern_len);
  mrb_naraku_warning_context_t* warning_context = NULL;
  nk_warning_func_t warning_callback = NULL;
  if (!mrb_nil_p(warning_func)) {
    warning_context = (mrb_naraku_warning_context_t*)mrb_malloc(mrb, sizeof(mrb_naraku_warning_context_t));
    warning_context->mrb = mrb;
    warning_context->warning_func = warning_func;
    warning_callback = mrb_naraku_warning_callback;
  }

  nk_parser_options_t options = {
    .is_extended_mode = is_extended_mode ? true : false,
    .is_ignore_case = is_ignore_case ? true : false,
    .dot_allows_newline = dot_allows_newline ? true : false,
    .char_class_is_strict = char_class_is_strict ? true : false,
    .char_type_is_ascii_only = char_type_is_ascii_only ? true : false,
    .posix_char_class_is_ascii_only = posix_char_class_is_ascii_only ? true : false,
    .fold_flags = (nk_fold_flag_t)fold_flags,
    .range_quantifier_max_repetition = (uint32_t)range_quantifier_max_repetition,
    .bare_back_ref_max_num = (uint32_t)bare_back_ref_max_num,
    .max_group_num = (uint32_t)max_group_num,
    .back_ref_max_num = (uint32_t)back_ref_max_num,
    .max_capture_depth = (uint32_t)max_capture_depth,
    .max_parse_depth = (uint32_t)max_parse_depth,
    .warning_func = warning_callback,
    .user_data = warning_context,
  };

  nk_error_t err = nk_parser_init(enc, pattern_copy, pattern_copy + pattern_len, options, parser);
  if (err != NK_SUCCESS) {
    if (warning_context != NULL) {
      mrb_free(mrb, warning_context);
    }
    struct RClass* naraku_module = mrb_module_get(mrb, "Naraku");
    struct RClass* error_class = mrb_class_get_under(mrb, naraku_module, "Error");
    mrb_raise(mrb, error_class, (const char*)nk_error_message(err));
  }

  struct RClass* parser_class = mrb_class_ptr(self);
  mrb_value obj = mrb_obj_value(mrb_data_object_alloc(mrb, parser_class, parser, &mrb_naraku_parser_type));
  if (!mrb_nil_p(warning_func)) {
    mrb_iv_set(mrb, obj, mrb_intern_lit(mrb, "@warning_func"), warning_func);
  }
  return obj;
}

static mrb_value mrb_naraku_parser_parse(mrb_state* mrb, mrb_value self) {
  nk_parser_t* parser = mrb_naraku_parser_get_ptr(mrb, self);

  nk_node_t* node = NULL;
  nk_error_t err = nk_parser_parse(parser, &node);
  if (err != NK_SUCCESS) {
    struct RClass* naraku_module = mrb_module_get(mrb, "Naraku");
    struct RClass* parse_error_class = mrb_class_get_under(mrb, naraku_module, "ParseError");
    mrb_value args[4];
    args[0] = self;
    args[1] = mrb_fixnum_value(err);
    mrb_int offset = 0;
    mrb_int length = 0;
    if (parser->error_bytes != NULL && parser->error_bytes >= parser->pattern_bytes_begin) {
      offset = (mrb_int)(parser->error_bytes - parser->pattern_bytes_begin);
      if (parser->error_bytes_end != NULL && parser->error_bytes_end >= parser->error_bytes) {
        length = (mrb_int)(parser->error_bytes_end - parser->error_bytes);
      }
    }
    args[2] = mrb_fixnum_value(offset);
    args[3] = mrb_fixnum_value(length);
    mrb_value exc = mrb_obj_new(mrb, parse_error_class, 4, args);
    mrb_exc_raise(mrb, exc);
  }

  if (node == NULL) {
    return mrb_nil_value();
  }

  return mrb_naraku_node_create_root(mrb, node);
}

// ============================================================================
//
// `gem_init` for `Naraku::Parser`:
//
// ============================================================================

void mrb_naraku_parser_gem_init(mrb_state* mrb, struct RClass* naraku_module) {
  struct RClass* parser_class = mrb_define_class_under(mrb, naraku_module, "Parser", mrb->object_class);
  MRB_SET_INSTANCE_TT(parser_class, MRB_TT_DATA);
  MRB_UNDEF_ALLOCATOR(parser_class);
  mrb_undef_class_method_id(mrb, parser_class, MRB_SYM(new));
  mrb_undef_class_method_id(mrb, parser_class, MRB_SYM(allocate));

  mrb_define_const(
    mrb,
    parser_class,
    "DEFAULT_RANGE_QUANTIFIER_MAX_REPETITION",
    mrb_fixnum_value(NK_DEFAULT_RANGE_QUANTIFIER_MAX_REPETITION)
  );
  mrb_define_const(
    mrb,
    parser_class,
    "DEFAULT_BARE_BACK_REF_MAX_NUM",
    mrb_fixnum_value(NK_DEFAULT_BARE_BACK_REF_MAX_NUM)
  );
  mrb_define_const(mrb, parser_class, "DEFAULT_MAX_GROUP_NUM", mrb_fixnum_value(NK_DEFAULT_MAX_GROUP_NUM));
  mrb_define_const(mrb, parser_class, "DEFAULT_BACK_REF_MAX_NUM", mrb_fixnum_value(NK_DEFAULT_BACK_REF_MAX_NUM));
  mrb_define_const(mrb, parser_class, "DEFAULT_MAX_CAPTURE_DEPTH", mrb_fixnum_value(NK_DEFAULT_MAX_CAPTURE_DEPTH));
  mrb_define_const(mrb, parser_class, "DEFAULT_MAX_PARSE_DEPTH", mrb_fixnum_value(NK_DEFAULT_MAX_PARSE_DEPTH));

  mrb_define_class_method(mrb, parser_class, "_new", mrb_naraku_parser_new, MRB_ARGS_REQ(16));
  mrb_define_method(mrb, parser_class, "parse", mrb_naraku_parser_parse, MRB_ARGS_NONE());
}
