#include <string.h>

#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/presym.h>
#include <mruby/value.h>

#include "mrb_naraku.h"

// ============================================================================
//
// `Naraku::Parser` class:
//
// ============================================================================

static void mrb_naraku_parser_free(mrb_state* mrb, void* ptr) {
  nk_parser_t* parser = (nk_parser_t*)ptr;
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

  // TODO: add `warning_func` argument for receiving warnings during parsing

  mrb_get_args(
    mrb,
    "dsbbbbbbi",
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
    &fold_flags
  );

  const nk_encoding_t* enc = (const nk_encoding_t*)encoding_ptr;

  nk_parser_t* parser = mrb_malloc(mrb, sizeof(nk_parser_t));
  uint8_t* pattern_copy = (uint8_t*)mrb_malloc(mrb, (size_t)pattern_len);
  memcpy(pattern_copy, pattern, (size_t)pattern_len);

  nk_parser_options_t options = {
    .is_extended_mode = is_extended_mode ? true : false,
    .is_ignore_case = is_ignore_case ? true : false,
    .dot_allows_newline = dot_allows_newline ? true : false,
    .char_class_is_strict = char_class_is_strict ? true : false,
    .char_type_is_ascii_only = char_type_is_ascii_only ? true : false,
    .posix_char_class_is_ascii_only = posix_char_class_is_ascii_only ? true : false,
    .fold_flags = (nk_fold_flag_t)fold_flags,
  };

  nk_error_t err = nk_parser_init(enc, pattern_copy, pattern_copy + pattern_len, options, parser);
  if (err != NK_SUCCESS) {
    mrb_raisef(mrb, E_ARGUMENT_ERROR, "failed to initialize parser: %d", err);
  }

  struct RClass* parser_class = mrb_class_ptr(self);
  return mrb_obj_value(mrb_data_object_alloc(mrb, parser_class, parser, &mrb_naraku_parser_type));
}

static mrb_value mrb_naraku_parser_parse(mrb_state* mrb, mrb_value self) {
  nk_parser_t* parser = mrb_naraku_parser_get_ptr(mrb, self);

  nk_node_t* node = NULL;
  nk_error_t err = nk_parser_parse(parser, &node);
  if (err != NK_SUCCESS) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "failed to parse pattern: %d", err);
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

  mrb_define_class_method(mrb, parser_class, "_new", mrb_naraku_parser_new, MRB_ARGS_REQ(9));
  mrb_define_method(mrb, parser_class, "parse", mrb_naraku_parser_parse, MRB_ARGS_NONE());
}
