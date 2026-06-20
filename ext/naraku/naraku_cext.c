/**
 * @file naraku_cext.c
 *
 * Minimal CRuby native extension wrapping the same C core (src/regex_compile.c,
 * src/regex_vm.c, src/parse.c, ...) that mrbgems/mruby-naraku binds for mruby.
 *
 * Purpose: let Naraku's engine be benchmarked under CRuby (+YJIT) so comparisons
 * against Onigmo are not skewed by mruby's lack of a JIT. Only the surface needed
 * for benchmarking is exposed: `Naraku::Regexp.new(pattern)` and `#match?(string,
 * start = 0)`. Full API parity (MatchData, named captures, flags) is intentionally
 * out of scope here; see mrbgems/mruby-naraku for the full-featured binding.
 */

#include <ruby.h>

#include <naraku_regex.h>

static void naraku_program_free(void* ptr) {
  nk_program_free((nk_program_t*)ptr);
}

static const rb_data_type_t naraku_program_type = {
  "Naraku::Regexp",
  {NULL, naraku_program_free, NULL},
  0,
  0,
  RUBY_TYPED_FREE_IMMEDIATELY,
};

static VALUE naraku_regexp_alloc(VALUE klass) {
  return TypedData_Wrap_Struct(klass, &naraku_program_type, NULL);
}

static nk_program_t* naraku_regexp_get_program(VALUE self) {
  nk_program_t* program;
  TypedData_Get_Struct(self, nk_program_t, &naraku_program_type, program);
  if (program == NULL) {
    rb_raise(rb_eRuntimeError, "Naraku::Regexp used before #initialize completed");
  }
  return program;
}

static void naraku_raise(nk_error_t err) {
  rb_raise(rb_eRuntimeError, "%s", (const char*)nk_error_message(err));
}

// `Naraku::Regexp.new(pattern)` → compiles `pattern` (UTF-8) into a VM program.
static VALUE naraku_regexp_initialize(VALUE self, VALUE pattern) {
  pattern = rb_str_to_str(pattern);
  const uint8_t* bytes = (const uint8_t*)RSTRING_PTR(pattern);
  const uint8_t* bytes_end = bytes + RSTRING_LEN(pattern);

  nk_parser_t parser;
  nk_parser_options_t options = nk_parser_options_default();
  nk_error_t err = nk_parser_init(nk_enc_utf_8, bytes, bytes_end, options, &parser);
  if (err != NK_SUCCESS) {
    naraku_raise(err);
  }

  nk_node_t* node = NULL;
  err = nk_parser_parse(&parser, &node);
  if (err != NK_SUCCESS) {
    nk_parser_free(&parser);
    naraku_raise(err);
  }

  if (node != NULL) {
    err = nk_parser_postprocess(&parser, node);
    if (err != NK_SUCCESS) {
      nk_node_free(node);
      nk_parser_free(&parser);
      naraku_raise(err);
    }
  }

  nk_program_t* program = NULL;
  size_t error_offset = 0;
  size_t error_length = 0;
  err = nk_program_compile(
    nk_enc_utf_8,
    node,
    parser.num_capture_groups,
    parser.capture_entries,
    parser.capture_entries_len,
    &program,
    &error_offset,
    &error_length
  );

  nk_node_free(node);
  nk_parser_free(&parser);

  if (err != NK_SUCCESS) {
    naraku_raise(err);
  }

  RTYPEDDATA_DATA(self) = program;
  return self;
}

// `regexp.match?(string, start = 0)` → true/false, no capture allocation.
static VALUE naraku_regexp_match_p(int argc, VALUE* argv, VALUE self) {
  VALUE str;
  VALUE start;
  rb_scan_args(argc, argv, "11", &str, &start);

  nk_program_t* program = naraku_regexp_get_program(self);

  str = rb_str_to_str(str);
  long start_offset = NIL_P(start) ? 0 : NUM2LONG(start);
  long subject_len = RSTRING_LEN(str);
  if (start_offset < 0 || start_offset > subject_len) {
    return Qfalse;
  }

  const uint8_t* bytes = (const uint8_t*)RSTRING_PTR(str);
  const uint8_t* bytes_end = bytes + subject_len;

  nk_error_t err = nk_program_search_boolean(program, bytes, bytes_end, (size_t)start_offset);
  if (err == NK_SUCCESS) {
    return Qtrue;
  }
  if (err == NK_NO_MATCH) {
    return Qfalse;
  }
  naraku_raise(err);
  return Qfalse;
}

void Init_naraku_cext(void) {
  VALUE naraku_module = rb_define_module("Naraku");
  VALUE regexp_class = rb_define_class_under(naraku_module, "Regexp", rb_cObject);
  rb_define_alloc_func(regexp_class, naraku_regexp_alloc);
  rb_define_method(regexp_class, "initialize", naraku_regexp_initialize, 1);
  rb_define_method(regexp_class, "match?", naraku_regexp_match_p, -1);
}
