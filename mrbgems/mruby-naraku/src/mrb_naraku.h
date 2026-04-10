#ifndef MRB_NARAKU_H
#define MRB_NARAKU_H

#include <mruby.h>
#include <mruby/data.h>

#include <naraku_encoding.h>
#include <naraku_encoding_internal.h>
#include <naraku_syntax.h>

// ============================================================================
//
// Shared data types:
//
// ============================================================================

extern struct mrb_data_type mrb_naraku_encoding_type;
extern struct mrb_data_type mrb_naraku_encoding_adjust_mbc_head_context_type;
extern struct mrb_data_type mrb_naraku_parser_type;

// ============================================================================
//
// Sub-module initializers:
//
// ============================================================================

void mrb_naraku_encoding_gem_init(mrb_state* mrb, struct RClass* naraku_module);
void mrb_naraku_node_gem_init(mrb_state* mrb, struct RClass* naraku_module);
void mrb_naraku_parser_gem_init(mrb_state* mrb, struct RClass* naraku_module);

// ============================================================================
//
// Node binding types (needed by parser to create nodes):
//
// ============================================================================

mrb_value mrb_naraku_node_create_root(mrb_state* mrb, nk_node_t* node);

#endif  // MRB_NARAKU_H
