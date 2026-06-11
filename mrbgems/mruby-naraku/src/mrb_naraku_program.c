/**
 * @file mrb_naraku_program.c
 */

#include <stdlib.h>
#include <string.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/presym.h>
#include <mruby/string.h>
#include <mruby/value.h>
#include <mruby/variable.h>

#include "mrb_naraku.h"
#include <naraku_regex.h>

// ============================================================================
//
// `Naraku::Program` class:
//
// ============================================================================

static void mrb_naraku_program_free(mrb_state* mrb, void* ptr) {
  (void)mrb;
  nk_program_free((nk_program_t*)ptr);
}

static struct mrb_data_type mrb_naraku_program_type = {"Program", mrb_naraku_program_free};

// `Program._compile(parser, node)` → Program instance
// Raises `Naraku::CompileError` on failure.
static mrb_value mrb_naraku_program_compile(mrb_state* mrb, mrb_value self) {
  mrb_value parser_obj;
  mrb_value node_obj;
  mrb_get_args(mrb, "oo", &parser_obj, &node_obj);

  nk_parser_t* parser = mrb_naraku_parser_get_ptr(mrb, parser_obj);
  nk_node_t* root_node = NULL;
  if (!mrb_nil_p(node_obj)) {
    root_node = mrb_naraku_node_get_ptr(mrb, node_obj);
  }

  nk_program_t* program = NULL;
  size_t error_offset = 0;
  size_t error_length = 0;
  nk_error_t err =
    nk_program_compile(parser->enc, root_node, parser->num_capture_groups, &program, &error_offset, &error_length);

  if (err != NK_SUCCESS) {
    struct RClass* naraku_module = mrb_module_get(mrb, "Naraku");
    struct RClass* compile_error_class = mrb_class_get_under(mrb, naraku_module, "CompileError");
    mrb_value args[3];
    args[0] = mrb_fixnum_value(err);
    args[1] = mrb_fixnum_value((mrb_int)error_offset);
    args[2] = mrb_fixnum_value((mrb_int)error_length);
    mrb_value exc = mrb_obj_new(mrb, compile_error_class, 3, args);
    mrb_exc_raise(mrb, exc);
  }

  struct RClass* program_class = mrb_class_ptr(self);
  mrb_value obj = mrb_obj_value(mrb_data_object_alloc(mrb, program_class, program, &mrb_naraku_program_type));
  mrb_iv_set(mrb, obj, mrb_intern_lit(mrb, "_parser"), parser_obj);
  return obj;
}

// `program._search(string, byte_start)` → Integer array (byte offsets) or nil
static mrb_value mrb_naraku_program_search(mrb_state* mrb, mrb_value self) {
  nk_program_t* program = (nk_program_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_program_type);

  char* subject;
  mrb_int subject_len;
  mrb_int start_offset;
  mrb_get_args(mrb, "si", &subject, &subject_len, &start_offset);

  if (start_offset < 0 || start_offset > subject_len) {
    return mrb_nil_value();
  }

  const uint8_t* bytes = (const uint8_t*)subject;
  const uint8_t* bytes_end = bytes + (size_t)subject_len;

  nk_region_t region;
  nk_error_t init_err = nk_region_init(&region, program->num_capture_groups);
  if (init_err != NK_SUCCESS) {
    mrb_raise(mrb, mrb->eStandardError_class, "failed to initialize region");
  }

  nk_error_t err = nk_program_search(program, bytes, bytes_end, (size_t)start_offset, &region);

  if (err == NK_NO_MATCH) {
    nk_region_free(&region);
    return mrb_nil_value();
  }

  if (err != NK_SUCCESS) {
    nk_region_free(&region);
    struct RClass* naraku_module = mrb_module_get(mrb, "Naraku");
    struct RClass* error_class = mrb_class_get_under(mrb, naraku_module, "Error");
    mrb_raise(mrb, error_class, (const char*)nk_error_message(err));
  }

  // Return flat array: [begin_0, end_0, begin_1, end_1, ...]
  // NK_REGION_POS_NONE is returned as nil.
  size_t total = region.num_caps * 2;
  mrb_value ary = mrb_ary_new_capa(mrb, (mrb_int)total);
  for (size_t i = 0; i < total; i++) {
    if (region.caps[i] == NK_REGION_POS_NONE) {
      mrb_ary_push(mrb, ary, mrb_nil_value());
    } else {
      mrb_ary_push(mrb, ary, mrb_fixnum_value((mrb_int)region.caps[i]));
    }
  }

  nk_region_free(&region);
  return ary;
}

// `program._search_boolean(string, byte_start)` → true or false
// Like _search but skips all capture allocation (for match?).
static mrb_value mrb_naraku_program_search_boolean(mrb_state* mrb, mrb_value self) {
  nk_program_t* program = (nk_program_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_program_type);

  char* subject;
  mrb_int subject_len;
  mrb_int start_offset;
  mrb_get_args(mrb, "si", &subject, &subject_len, &start_offset);

  if (start_offset < 0 || start_offset > subject_len) {
    return mrb_false_value();
  }

  const uint8_t* bytes = (const uint8_t*)subject;
  const uint8_t* bytes_end = bytes + (size_t)subject_len;

  nk_error_t err = nk_program_search_boolean(program, bytes, bytes_end, (size_t)start_offset);

  if (err == NK_SUCCESS) {
    return mrb_true_value();
  }
  if (err == NK_NO_MATCH) {
    return mrb_false_value();
  }
  struct RClass* naraku_module = mrb_module_get(mrb, "Naraku");
  struct RClass* error_class = mrb_class_get_under(mrb, naraku_module, "Error");
  mrb_raise(mrb, error_class, (const char*)nk_error_message(err));
}

static mrb_value mrb_naraku_program_num_capture_groups(mrb_state* mrb, mrb_value self) {
  nk_program_t* program = (nk_program_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_program_type);
  return mrb_fixnum_value((mrb_int)program->num_capture_groups);
}

// ============================================================================
//
// `gem_init` for `Naraku::Program`:
//
// ============================================================================

void mrb_naraku_program_gem_init(mrb_state* mrb, struct RClass* naraku_module) {
  struct RClass* program_class = mrb_define_class_under(mrb, naraku_module, "Program", mrb->object_class);
  MRB_SET_INSTANCE_TT(program_class, MRB_TT_DATA);
  MRB_UNDEF_ALLOCATOR(program_class);
  mrb_undef_class_method_id(mrb, program_class, MRB_SYM(new));
  mrb_undef_class_method_id(mrb, program_class, MRB_SYM(allocate));

  mrb_define_class_method(mrb, program_class, "_compile", mrb_naraku_program_compile, MRB_ARGS_REQ(2));
  mrb_define_method(mrb, program_class, "_search", mrb_naraku_program_search, MRB_ARGS_REQ(2));
  mrb_define_method(mrb, program_class, "_search_boolean", mrb_naraku_program_search_boolean, MRB_ARGS_REQ(2));
  mrb_define_method(mrb, program_class, "num_capture_groups", mrb_naraku_program_num_capture_groups, MRB_ARGS_NONE());
}
