#include <mruby.h>

#include "mrb_naraku.h"

void mrb_mruby_naraku_gem_init(mrb_state* mrb) {
  struct RClass* naraku_module = mrb_define_module(mrb, "Naraku");

  mrb_naraku_encoding_gem_init(mrb, naraku_module);
  mrb_naraku_node_gem_init(mrb, naraku_module);
  mrb_naraku_parser_gem_init(mrb, naraku_module);
}

void mrb_mruby_naraku_gem_final(mrb_state* mrb) {
}
