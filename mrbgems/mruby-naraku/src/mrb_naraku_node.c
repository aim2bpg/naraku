#include <stdlib.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/presym.h>
#include <mruby/value.h>
#include <mruby/variable.h>

#include "mrb_naraku.h"

// ============================================================================
//
// `Naraku::Node` class:
//
// ============================================================================

/**
 * Wrapper for the root node of the regex AST.
 */
typedef struct {
  nk_node_t* root;
} mrb_naraku_node_root_t;

static void mrb_naraku_node_root_free(mrb_state* mrb, void* ptr) {
  mrb_naraku_node_root_t* root = (mrb_naraku_node_root_t*)ptr;

  if (root->root != NULL) {
    nk_node_free(root->root);
    root->root = NULL;
  }

  mrb_free(mrb, root);
}
static struct mrb_data_type mrb_naraku_node_root_type = {"Node::Root", mrb_naraku_node_root_free};

/**
 * Wrapper for a regex AST node that holds a reference to the root wrapper to
 * prevent it from being garbage collected.
 */
typedef struct {
  nk_node_t* node;
  mrb_value root_ref;  // mrb_value of the `Naraku::Node::Root` instance
} mrb_naraku_node_t;

static void mrb_naraku_node_free_func(mrb_state* mrb, void* ptr) {
  mrb_naraku_node_t* wrapper = (mrb_naraku_node_t*)ptr;

  // Do not free node itself; the root owns it.
  mrb_free(mrb, wrapper);
}
static struct mrb_data_type mrb_naraku_node_type = {"Node", mrb_naraku_node_free_func};

static struct RClass* mrb_naraku_node_class(mrb_state* mrb) {
  struct RClass* naraku = mrb_module_get(mrb, "Naraku");
  return mrb_class_get_under(mrb, naraku, "Node");
}

static struct RClass* mrb_naraku_node_root_class(mrb_state* mrb) {
  return mrb_class_get_under(mrb, mrb_naraku_node_class(mrb), "Root");
}

static struct RClass* mrb_naraku_char_class_union_class(mrb_state* mrb) {
  return mrb_class_get_under(mrb, mrb_naraku_node_class(mrb), "CharClassUnion");
}

static struct RClass* mrb_naraku_char_class_item_class(mrb_state* mrb) {
  return mrb_class_get_under(mrb, mrb_naraku_node_class(mrb), "CharClassItem");
}

/**
 * Wraps an `nk_node_t*` as a `Naraku::Node`, holding a reference to the root.
 */
static mrb_value mrb_naraku_node_wrap(mrb_state* mrb, nk_node_t* node, mrb_value root_ref) {
  if (node == NULL) {
    return mrb_nil_value();
  }

  mrb_naraku_node_t* wrapper = (mrb_naraku_node_t*)mrb_malloc(mrb, sizeof(mrb_naraku_node_t));
  wrapper->node = node;
  wrapper->root_ref = root_ref;

  mrb_value obj = mrb_obj_value(mrb_data_object_alloc(mrb, mrb_naraku_node_class(mrb), wrapper, &mrb_naraku_node_type));

  // Store `root_ref` as an instance variable to prevent GC of the root.
  // NOTE: Non-`@`-prefixed name is used to avoid referencing the root from user code.
  mrb_iv_set(mrb, obj, mrb_intern_cstr(mrb, "_root"), root_ref);

  return obj;
}

/**
 * Creates a root-owned Node from a freshly parsed `nk_node_t*`.
 */
mrb_value mrb_naraku_node_create_root(mrb_state* mrb, nk_node_t* node) {
  // Convert all pbuf views to owned.
  nk_error_t err = nk_node_to_owned(node);
  if (err != NK_SUCCESS) {
    nk_node_free(node);
    free(node);
    mrb_raise(mrb, E_RUNTIME_ERROR, "failed to convert node buffers to owned");
  }

  // Allocate the root wrapper.
  mrb_naraku_node_root_t* root = (mrb_naraku_node_root_t*)mrb_malloc(mrb, sizeof(mrb_naraku_node_root_t));
  root->root = node;
  mrb_value root_ref =
    mrb_obj_value(mrb_data_object_alloc(mrb, mrb_naraku_node_root_class(mrb), root, &mrb_naraku_node_root_type));

  return mrb_naraku_node_wrap(mrb, node, root_ref);
}

static nk_node_t* mrb_naraku_node_get_ptr(mrb_state* mrb, mrb_value self) {
  mrb_naraku_node_t* wrapper = (mrb_naraku_node_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_node_type);
  return wrapper->node;
}

static mrb_value mrb_naraku_node_get_root_ref(mrb_state* mrb, mrb_value self) {
  mrb_naraku_node_t* wrapper = (mrb_naraku_node_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_node_type);
  return wrapper->root_ref;
}

static mrb_value mrb_naraku_pbuf_to_str(mrb_state* mrb, const nk_pbuf_t* pbuf) {
  size_t len = (size_t)(pbuf->bytes_end - pbuf->bytes);
  return mrb_str_new(mrb, (const char*)pbuf->bytes, len);
}

// ============================================================================
//
// `Naraku::Node::CharClassUnion` and `Naraku::Node::CharClassItem` classes:
//
// ============================================================================

typedef struct {
  nk_char_class_union_t* u;
  mrb_value root_ref;
} mrb_naraku_char_class_union_t;

typedef struct {
  nk_char_class_item_t* item;
  mrb_value root_ref;
} mrb_naraku_char_class_item_t;

static void mrb_naraku_char_class_union_free_func(mrb_state* mrb, void* ptr) {
  mrb_free(mrb, ptr);
}
static struct mrb_data_type mrb_naraku_char_class_union_type = {
  "CharClassUnion",
  mrb_naraku_char_class_union_free_func
};

static void mrb_naraku_char_class_item_free_func(mrb_state* mrb, void* ptr) {
  mrb_free(mrb, ptr);
}
static struct mrb_data_type mrb_naraku_char_class_item_data_type = {
  "CharClassItem",
  mrb_naraku_char_class_item_free_func
};

static mrb_value mrb_naraku_char_class_union_wrap(mrb_state* mrb, nk_char_class_union_t* u, mrb_value root_ref) {
  mrb_naraku_char_class_union_t* wrapper =
    (mrb_naraku_char_class_union_t*)mrb_malloc(mrb, sizeof(mrb_naraku_char_class_union_t));
  wrapper->u = u;
  wrapper->root_ref = root_ref;
  mrb_value obj = mrb_obj_value(
    mrb_data_object_alloc(mrb, mrb_naraku_char_class_union_class(mrb), wrapper, &mrb_naraku_char_class_union_type)
  );
  mrb_iv_set(mrb, obj, mrb_intern_cstr(mrb, "_root"), root_ref);
  return obj;
}

static mrb_value mrb_naraku_char_class_item_wrap(mrb_state* mrb, nk_char_class_item_t* item, mrb_value root_ref) {
  mrb_naraku_char_class_item_t* wrapper =
    (mrb_naraku_char_class_item_t*)mrb_malloc(mrb, sizeof(mrb_naraku_char_class_item_t));
  wrapper->item = item;
  wrapper->root_ref = root_ref;
  mrb_value obj = mrb_obj_value(
    mrb_data_object_alloc(mrb, mrb_naraku_char_class_item_class(mrb), wrapper, &mrb_naraku_char_class_item_data_type)
  );
  mrb_iv_set(mrb, obj, mrb_intern_cstr(mrb, "_root"), root_ref);
  return obj;
}

// ============================================================================
//
// `Naraku::Node::CharClassUnion` accessor methods:
//
// ============================================================================

static mrb_value mrb_naraku_char_class_union_items(mrb_state* mrb, mrb_value self) {
  mrb_naraku_char_class_union_t* wrapper =
    (mrb_naraku_char_class_union_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_char_class_union_type);
  nk_char_class_union_t* u = wrapper->u;
  mrb_value root_ref = wrapper->root_ref;

  mrb_value ary = mrb_ary_new_capa(mrb, u->items_len);
  for (size_t i = 0; i < u->items_len; i++) {
    mrb_ary_push(mrb, ary, mrb_naraku_char_class_item_wrap(mrb, u->items[i], root_ref));
  }
  return ary;
}

// ============================================================================
//
// `Naraku::Node::CharClassItem` accessor methods:
//
// ============================================================================

static const char* char_class_item_type_name(nk_char_class_item_type_t type) {
  switch (type) {
    case NK_CHAR_CLASS_ITEM_TYPE_CODE:
      return "code";
    case NK_CHAR_CLASS_ITEM_TYPE_RANGE:
      return "range";
    case NK_CHAR_CLASS_ITEM_TYPE_CHAR_TYPE:
      return "char_type";
    case NK_CHAR_CLASS_ITEM_TYPE_POSIX_CHAR_CLASS:
      return "posix_char_class";
    case NK_CHAR_CLASS_ITEM_TYPE_CHAR_PROP:
      return "char_prop";
    case NK_CHAR_CLASS_ITEM_TYPE_NESTED_CHAR_CLASS:
      return "nested_char_class";
  }
  return "unknown";
}

static mrb_value mrb_naraku_char_class_item_type(mrb_state* mrb, mrb_value self) {
  mrb_naraku_char_class_item_t* wrapper =
    (mrb_naraku_char_class_item_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_char_class_item_data_type);
  return mrb_symbol_value(mrb_intern_cstr(mrb, char_class_item_type_name(wrapper->item->type)));
}

#define CHAR_CLASS_ITEM_CHECK_TYPE(mrb, wrapper, expected, method_name) \
  if (wrapper->item->type != (expected)) {                              \
    mrb_raisef(                                                         \
      mrb,                                                              \
      E_RUNTIME_ERROR,                                                  \
      #method_name " is not available for %s char class item",          \
      char_class_item_type_name(wrapper->item->type)                    \
    );                                                                  \
  }

static mrb_value mrb_naraku_char_class_item_code(mrb_state* mrb, mrb_value self) {
  mrb_naraku_char_class_item_t* wrapper =
    (mrb_naraku_char_class_item_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_char_class_item_data_type);
  CHAR_CLASS_ITEM_CHECK_TYPE(mrb, wrapper, NK_CHAR_CLASS_ITEM_TYPE_CODE, code);
  return mrb_fixnum_value(wrapper->item->data.code);
}

static mrb_value mrb_naraku_char_class_item_from_code(mrb_state* mrb, mrb_value self) {
  mrb_naraku_char_class_item_t* wrapper =
    (mrb_naraku_char_class_item_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_char_class_item_data_type);
  CHAR_CLASS_ITEM_CHECK_TYPE(mrb, wrapper, NK_CHAR_CLASS_ITEM_TYPE_RANGE, from_code);
  return mrb_fixnum_value(wrapper->item->data.range.from_code);
}

static mrb_value mrb_naraku_char_class_item_to_code(mrb_state* mrb, mrb_value self) {
  mrb_naraku_char_class_item_t* wrapper =
    (mrb_naraku_char_class_item_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_char_class_item_data_type);
  CHAR_CLASS_ITEM_CHECK_TYPE(mrb, wrapper, NK_CHAR_CLASS_ITEM_TYPE_RANGE, to_code);
  return mrb_fixnum_value(wrapper->item->data.range.to_code);
}

static mrb_value mrb_naraku_char_class_item_is_positive(mrb_state* mrb, mrb_value self) {
  mrb_naraku_char_class_item_t* wrapper =
    (mrb_naraku_char_class_item_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_char_class_item_data_type);
  switch (wrapper->item->type) {
    case NK_CHAR_CLASS_ITEM_TYPE_CHAR_TYPE:
      return mrb_bool_value(wrapper->item->data.char_type.is_positive);
    case NK_CHAR_CLASS_ITEM_TYPE_POSIX_CHAR_CLASS:
      return mrb_bool_value(wrapper->item->data.posix_char_class.is_positive);
    case NK_CHAR_CLASS_ITEM_TYPE_CHAR_PROP:
      return mrb_bool_value(wrapper->item->data.char_prop.is_positive);
    case NK_CHAR_CLASS_ITEM_TYPE_NESTED_CHAR_CLASS:
      return mrb_bool_value(wrapper->item->data.nested_char_class.is_positive);
    default:
      mrb_raisef(
        mrb,
        E_RUNTIME_ERROR,
        "is_positive is not available for %s char class item",
        char_class_item_type_name(wrapper->item->type)
      );
      return mrb_nil_value();
  }
}

static mrb_value mrb_naraku_char_class_item_is_ascii_only(mrb_state* mrb, mrb_value self) {
  mrb_naraku_char_class_item_t* wrapper =
    (mrb_naraku_char_class_item_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_char_class_item_data_type);
  switch (wrapper->item->type) {
    case NK_CHAR_CLASS_ITEM_TYPE_CHAR_TYPE:
      return mrb_bool_value(wrapper->item->data.char_type.is_ascii_only);
    case NK_CHAR_CLASS_ITEM_TYPE_POSIX_CHAR_CLASS:
      return mrb_bool_value(wrapper->item->data.posix_char_class.is_ascii_only);
    default:
      mrb_raisef(
        mrb,
        E_RUNTIME_ERROR,
        "is_ascii_only is not available for %s char class item",
        char_class_item_type_name(wrapper->item->type)
      );
      return mrb_nil_value();
  }
}

static const char* char_type_sym_name(nk_char_type_t ct) {
  switch (ct) {
    case NK_CHAR_TYPE_WORD:
      return "word";
    case NK_CHAR_TYPE_DIGIT:
      return "digit";
    case NK_CHAR_TYPE_SPACE:
      return "space";
    case NK_CHAR_TYPE_HEX_DIGIT:
      return "hex_digit";
  }
  return "unknown";
}

static mrb_value mrb_naraku_char_class_item_char_type(mrb_state* mrb, mrb_value self) {
  mrb_naraku_char_class_item_t* wrapper =
    (mrb_naraku_char_class_item_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_char_class_item_data_type);
  CHAR_CLASS_ITEM_CHECK_TYPE(mrb, wrapper, NK_CHAR_CLASS_ITEM_TYPE_CHAR_TYPE, char_type);
  return mrb_symbol_value(mrb_intern_cstr(mrb, char_type_sym_name(wrapper->item->data.char_type.char_type)));
}

static const char* posix_char_class_sym_name(nk_posix_char_class_t pc) {
  switch (pc) {
    case NK_POSIX_CHAR_CLASS_ALNUM:
      return "alnum";
    case NK_POSIX_CHAR_CLASS_ALPHA:
      return "alpha";
    case NK_POSIX_CHAR_CLASS_BLANK:
      return "blank";
    case NK_POSIX_CHAR_CLASS_CNTRL:
      return "cntrl";
    case NK_POSIX_CHAR_CLASS_DIGIT:
      return "digit";
    case NK_POSIX_CHAR_CLASS_GRAPH:
      return "graph";
    case NK_POSIX_CHAR_CLASS_LOWER:
      return "lower";
    case NK_POSIX_CHAR_CLASS_PRINT:
      return "print";
    case NK_POSIX_CHAR_CLASS_PUNCT:
      return "punct";
    case NK_POSIX_CHAR_CLASS_SPACE:
      return "space";
    case NK_POSIX_CHAR_CLASS_UPPER:
      return "upper";
    case NK_POSIX_CHAR_CLASS_XDIGIT:
      return "xdigit";
    case NK_POSIX_CHAR_CLASS_ASCII:
      return "ascii";
    case NK_POSIX_CHAR_CLASS_WORD:
      return "word";
  }
  return "unknown";
}

static mrb_value mrb_naraku_char_class_item_posix_char_class(mrb_state* mrb, mrb_value self) {
  mrb_naraku_char_class_item_t* wrapper =
    (mrb_naraku_char_class_item_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_char_class_item_data_type);
  CHAR_CLASS_ITEM_CHECK_TYPE(mrb, wrapper, NK_CHAR_CLASS_ITEM_TYPE_POSIX_CHAR_CLASS, posix_char_class);
  return mrb_symbol_value(
    mrb_intern_cstr(mrb, posix_char_class_sym_name(wrapper->item->data.posix_char_class.posix_char_class))
  );
}

static mrb_value mrb_naraku_char_class_item_cprop(mrb_state* mrb, mrb_value self) {
  mrb_naraku_char_class_item_t* wrapper =
    (mrb_naraku_char_class_item_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_char_class_item_data_type);
  CHAR_CLASS_ITEM_CHECK_TYPE(mrb, wrapper, NK_CHAR_CLASS_ITEM_TYPE_CHAR_PROP, cprop);
  return mrb_fixnum_value(wrapper->item->data.char_prop.cprop);
}

static mrb_value mrb_naraku_char_class_item_unions(mrb_state* mrb, mrb_value self) {
  mrb_naraku_char_class_item_t* wrapper =
    (mrb_naraku_char_class_item_t*)mrb_data_get_ptr(mrb, self, &mrb_naraku_char_class_item_data_type);
  CHAR_CLASS_ITEM_CHECK_TYPE(mrb, wrapper, NK_CHAR_CLASS_ITEM_TYPE_NESTED_CHAR_CLASS, unions);
  mrb_value root_ref = wrapper->root_ref;
  size_t len = wrapper->item->data.nested_char_class.unions_len;
  mrb_value ary = mrb_ary_new_capa(mrb, len);
  for (size_t i = 0; i < len; i++) {
    mrb_ary_push(
      mrb,
      ary,
      mrb_naraku_char_class_union_wrap(mrb, wrapper->item->data.nested_char_class.unions[i], root_ref)
    );
  }
  return ary;
}

// ============================================================================
//
// `Naraku::Node` accessor methods:
//
// ============================================================================

static const char* node_type_name(nk_node_type_t type) {
  switch (type) {
    case NK_NODE_TYPE_UNKNOWN:
      return "unknown";
    case NK_NODE_TYPE_LITERAL:
      return "literal";
    case NK_NODE_TYPE_CHAR_CLASS:
      return "char_class";
    case NK_NODE_TYPE_CHAR_TYPE:
      return "char_type";
    case NK_NODE_TYPE_CHAR_PROP:
      return "char_prop";
    case NK_NODE_TYPE_DOT:
      return "dot";
    case NK_NODE_TYPE_NEWLINE:
      return "newline";
    case NK_NODE_TYPE_GRAPHEME_CLUSTER:
      return "grapheme_cluster";
    case NK_NODE_TYPE_KEEP:
      return "keep";
    case NK_NODE_TYPE_BACK_REF:
      return "back_ref";
    case NK_NODE_TYPE_CALL:
      return "call";
    case NK_NODE_TYPE_ASSERTION:
      return "assertion";
    case NK_NODE_TYPE_QUANTIFIER:
      return "quantifier";
    case NK_NODE_TYPE_GROUP:
      return "group";
    case NK_NODE_TYPE_ATOMIC:
      return "atomic";
    case NK_NODE_TYPE_CONDITIONAL:
      return "conditional";
    case NK_NODE_TYPE_CONCAT:
      return "concat";
    case NK_NODE_TYPE_ALT:
      return "alt";
  }
  return "unknown";
}

#define NODE_CHECK_TYPE(mrb, node, expected, method_name)                                                            \
  if (node->base.type != (expected)) {                                                                               \
    mrb_raisef(mrb, E_RUNTIME_ERROR, #method_name " is not available for %s node", node_type_name(node->base.type)); \
  }

static mrb_value mrb_naraku_node_type_method(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  return mrb_symbol_value(mrb_intern_cstr(mrb, node_type_name(node->base.type)));
}

static mrb_value mrb_naraku_node_buf(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_LITERAL, buf);
  return mrb_naraku_pbuf_to_str(mrb, &node->literal.buf);
}

static mrb_value mrb_naraku_node_is_ignore_case(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  switch (node->base.type) {
    case NK_NODE_TYPE_LITERAL:
      return mrb_bool_value(node->literal.is_ignore_case);
    case NK_NODE_TYPE_CHAR_CLASS:
      return mrb_bool_value(node->char_class.is_ignore_case);
    case NK_NODE_TYPE_CHAR_TYPE:
      return mrb_bool_value(node->char_type.is_ignore_case);
    case NK_NODE_TYPE_CHAR_PROP:
      return mrb_bool_value(node->char_prop.is_ignore_case);
    case NK_NODE_TYPE_BACK_REF:
      return mrb_bool_value(node->back_ref.is_ignore_case);
    default:
      mrb_raisef(mrb, E_RUNTIME_ERROR, "is_ignore_case is not available for %s node", node_type_name(node->base.type));
      return mrb_nil_value();
  }
}

static mrb_value mrb_naraku_node_fold_flags(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  switch (node->base.type) {
    case NK_NODE_TYPE_LITERAL:
      return mrb_fixnum_value(node->literal.fold_flags);
    case NK_NODE_TYPE_CHAR_CLASS:
      return mrb_fixnum_value(node->char_class.fold_flags);
    case NK_NODE_TYPE_CHAR_TYPE:
      return mrb_fixnum_value(node->char_type.fold_flags);
    case NK_NODE_TYPE_CHAR_PROP:
      return mrb_fixnum_value(node->char_prop.fold_flags);
    case NK_NODE_TYPE_BACK_REF:
      return mrb_fixnum_value(node->back_ref.fold_flags);
    default:
      mrb_raisef(mrb, E_RUNTIME_ERROR, "fold_flags is not available for %s node", node_type_name(node->base.type));
      return mrb_nil_value();
  }
}

static mrb_value mrb_naraku_node_is_strict(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_CHAR_CLASS, is_strict);
  return mrb_bool_value(node->char_class.is_strict);
}

static mrb_value mrb_naraku_node_is_positive(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  switch (node->base.type) {
    case NK_NODE_TYPE_CHAR_CLASS:
      return mrb_bool_value(node->char_class.is_positive);
    case NK_NODE_TYPE_CHAR_TYPE:
      return mrb_bool_value(node->char_type.is_positive);
    case NK_NODE_TYPE_CHAR_PROP:
      return mrb_bool_value(node->char_prop.is_positive);
    default:
      mrb_raisef(mrb, E_RUNTIME_ERROR, "is_positive is not available for %s node", node_type_name(node->base.type));
      return mrb_nil_value();
  }
}

static mrb_value mrb_naraku_node_unions(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_CHAR_CLASS, unions);
  mrb_value root_ref = mrb_naraku_node_get_root_ref(mrb, self);
  mrb_value ary = mrb_ary_new_capa(mrb, node->char_class.unions_len);
  for (size_t i = 0; i < node->char_class.unions_len; i++) {
    mrb_ary_push(mrb, ary, mrb_naraku_char_class_union_wrap(mrb, node->char_class.unions[i], root_ref));
  }
  return ary;
}

static mrb_value mrb_naraku_node_is_ascii_only(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_CHAR_TYPE, is_ascii_only);
  return mrb_bool_value(node->char_type.is_ascii_only);
}

static mrb_value mrb_naraku_node_char_type(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_CHAR_TYPE, char_type);
  return mrb_symbol_value(mrb_intern_cstr(mrb, char_type_sym_name(node->char_type.char_type)));
}

static mrb_value mrb_naraku_node_cprop(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_CHAR_PROP, cprop);
  return mrb_fixnum_value(node->char_prop.cprop);
}

static mrb_value mrb_naraku_node_allows_newline(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_DOT, allows_newline);
  return mrb_bool_value(node->dot.allows_newline);
}

static mrb_value mrb_naraku_node_has_name(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  switch (node->base.type) {
    case NK_NODE_TYPE_BACK_REF:
      return mrb_bool_value(node->back_ref.has_name);
    case NK_NODE_TYPE_CALL:
      return mrb_bool_value(node->call.has_name);
    case NK_NODE_TYPE_GROUP:
      return mrb_bool_value(node->group.has_name);
    case NK_NODE_TYPE_CONDITIONAL:
      return mrb_bool_value(node->conditional.has_name);
    default:
      mrb_raisef(mrb, E_RUNTIME_ERROR, "has_name is not available for %s node", node_type_name(node->base.type));
      return mrb_nil_value();
  }
}

static mrb_value mrb_naraku_node_name(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  switch (node->base.type) {
    case NK_NODE_TYPE_BACK_REF:
      if (!node->back_ref.has_name) return mrb_nil_value();
      return mrb_naraku_pbuf_to_str(mrb, &node->back_ref.name_buf);
    case NK_NODE_TYPE_CALL:
      if (!node->call.has_name) return mrb_nil_value();
      return mrb_naraku_pbuf_to_str(mrb, &node->call.name_buf);
    case NK_NODE_TYPE_GROUP:
      if (!node->group.has_name) return mrb_nil_value();
      return mrb_naraku_pbuf_to_str(mrb, &node->group.name_buf);
    case NK_NODE_TYPE_CONDITIONAL:
      if (!node->conditional.has_name) return mrb_nil_value();
      return mrb_naraku_pbuf_to_str(mrb, &node->conditional.name_buf);
    default:
      mrb_raisef(mrb, E_RUNTIME_ERROR, "name is not available for %s node", node_type_name(node->base.type));
      return mrb_nil_value();
  }
}

static mrb_value mrb_naraku_node_group_num(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  switch (node->base.type) {
    case NK_NODE_TYPE_BACK_REF:
      return mrb_fixnum_value(node->back_ref.group_num);
    case NK_NODE_TYPE_CALL:
      return mrb_fixnum_value(node->call.group_num);
    case NK_NODE_TYPE_GROUP:
      return mrb_fixnum_value(node->group.group_num);
    case NK_NODE_TYPE_CONDITIONAL:
      return mrb_fixnum_value(node->conditional.group_num);
    default:
      mrb_raisef(mrb, E_RUNTIME_ERROR, "group_num is not available for %s node", node_type_name(node->base.type));
      return mrb_nil_value();
  }
}

static mrb_value mrb_naraku_node_depth(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_BACK_REF, depth);
  return mrb_fixnum_value(node->back_ref.depth);
}

static const char* assertion_type_sym_name(nk_assertion_type_t type) {
  switch (type) {
    case NK_ASSERTION_TYPE_BEGIN_OF_LINE:
      return "begin_of_line";
    case NK_ASSERTION_TYPE_END_OF_LINE:
      return "end_of_line";
    case NK_ASSERTION_TYPE_BEGIN_OF_STRING:
      return "begin_of_string";
    case NK_ASSERTION_TYPE_END_OF_STRING_STRICT:
      return "end_of_string_strict";
    case NK_ASSERTION_TYPE_END_OF_STRING_LOOSE:
      return "end_of_string_loose";
    case NK_ASSERTION_TYPE_BEGIN_OF_MATCHING:
      return "begin_of_matching";
    case NK_ASSERTION_TYPE_WORD_BOUNDARY:
      return "word_boundary";
    case NK_ASSERTION_TYPE_NON_WORD_BOUNDARY:
      return "non_word_boundary";
    case NK_ASSERTION_TYPE_POSITIVE_LOOKAHEAD:
      return "positive_lookahead";
    case NK_ASSERTION_TYPE_NEGATIVE_LOOKAHEAD:
      return "negative_lookahead";
    case NK_ASSERTION_TYPE_POSITIVE_LOOKBEHIND:
      return "positive_lookbehind";
    case NK_ASSERTION_TYPE_NEGATIVE_LOOKBEHIND:
      return "negative_lookbehind";
  }
  return "unknown";
}

static mrb_value mrb_naraku_node_assertion_type(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_ASSERTION, assertion_type);
  return mrb_symbol_value(mrb_intern_cstr(mrb, assertion_type_sym_name(node->assertion.type)));
}

static mrb_value mrb_naraku_node_child(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  mrb_value root_ref = mrb_naraku_node_get_root_ref(mrb, self);
  switch (node->base.type) {
    case NK_NODE_TYPE_ASSERTION:
      return mrb_naraku_node_wrap(mrb, node->assertion.child, root_ref);
    case NK_NODE_TYPE_QUANTIFIER:
      return mrb_naraku_node_wrap(mrb, node->quantifier.child, root_ref);
    case NK_NODE_TYPE_GROUP:
      return mrb_naraku_node_wrap(mrb, node->group.child, root_ref);
    case NK_NODE_TYPE_ATOMIC:
      return mrb_naraku_node_wrap(mrb, node->atomic.child, root_ref);
    default:
      mrb_raisef(mrb, E_RUNTIME_ERROR, "child is not available for %s node", node_type_name(node->base.type));
      return mrb_nil_value();
  }
}

static mrb_value mrb_naraku_node_min(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_QUANTIFIER, min);
  return mrb_fixnum_value(node->quantifier.min);
}

static mrb_value mrb_naraku_node_max(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_QUANTIFIER, max);
  return mrb_fixnum_value(node->quantifier.max);
}

static mrb_value mrb_naraku_node_quantifier_type(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_QUANTIFIER, quantifier_type);
  switch (node->quantifier.type) {
    case NK_QUANTIFIER_TYPE_GREEDY:
      return mrb_symbol_value(mrb_intern_cstr(mrb, "greedy"));
    case NK_QUANTIFIER_TYPE_RELUCTANT:
      return mrb_symbol_value(mrb_intern_cstr(mrb, "reluctant"));
    case NK_QUANTIFIER_TYPE_POSSESSIVE:
      return mrb_symbol_value(mrb_intern_cstr(mrb, "possessive"));
  }
  return mrb_nil_value();
}

static mrb_value mrb_naraku_node_yes_child(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_CONDITIONAL, yes_child);
  return mrb_naraku_node_wrap(mrb, node->conditional.yes_child, mrb_naraku_node_get_root_ref(mrb, self));
}

static mrb_value mrb_naraku_node_no_child(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  NODE_CHECK_TYPE(mrb, node, NK_NODE_TYPE_CONDITIONAL, no_child);
  return mrb_naraku_node_wrap(mrb, node->conditional.no_child, mrb_naraku_node_get_root_ref(mrb, self));
}

static mrb_value mrb_naraku_node_children(mrb_state* mrb, mrb_value self) {
  nk_node_t* node = mrb_naraku_node_get_ptr(mrb, self);
  mrb_value root_ref = mrb_naraku_node_get_root_ref(mrb, self);
  nk_node_t** children;
  size_t children_len;
  switch (node->base.type) {
    case NK_NODE_TYPE_CONCAT:
      children = node->concat.children;
      children_len = node->concat.children_len;
      break;
    case NK_NODE_TYPE_ALT:
      children = node->alt.children;
      children_len = node->alt.children_len;
      break;
    default:
      mrb_raisef(mrb, E_RUNTIME_ERROR, "children is not available for %s node", node_type_name(node->base.type));
      return mrb_nil_value();
  }

  mrb_value ary = mrb_ary_new_capa(mrb, children_len);
  for (size_t i = 0; i < children_len; i++) {
    mrb_ary_push(mrb, ary, mrb_naraku_node_wrap(mrb, children[i], root_ref));
  }
  return ary;
}

// ============================================================================
//
// `gem_init` for `Naraku::Node` and its related classes:
//
// ============================================================================

void mrb_naraku_node_gem_init(mrb_state* mrb, struct RClass* naraku_module) {
  // `Naraku::Node`:
  struct RClass* node_class = mrb_define_class_under(mrb, naraku_module, "Node", mrb->object_class);
  MRB_SET_INSTANCE_TT(node_class, MRB_TT_DATA);
  MRB_UNDEF_ALLOCATOR(node_class);
  mrb_undef_class_method_id(mrb, node_class, MRB_SYM(new));
  mrb_undef_class_method_id(mrb, node_class, MRB_SYM(allocate));

  mrb_define_method(mrb, node_class, "type", mrb_naraku_node_type_method, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "buf", mrb_naraku_node_buf, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "is_ignore_case", mrb_naraku_node_is_ignore_case, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "fold_flags", mrb_naraku_node_fold_flags, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "is_strict", mrb_naraku_node_is_strict, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "is_positive", mrb_naraku_node_is_positive, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "unions", mrb_naraku_node_unions, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "is_ascii_only", mrb_naraku_node_is_ascii_only, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "char_type", mrb_naraku_node_char_type, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "cprop", mrb_naraku_node_cprop, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "allows_newline", mrb_naraku_node_allows_newline, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "has_name", mrb_naraku_node_has_name, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "name", mrb_naraku_node_name, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "group_num", mrb_naraku_node_group_num, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "depth", mrb_naraku_node_depth, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "assertion_type", mrb_naraku_node_assertion_type, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "child", mrb_naraku_node_child, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "min", mrb_naraku_node_min, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "max", mrb_naraku_node_max, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "quantifier_type", mrb_naraku_node_quantifier_type, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "yes_child", mrb_naraku_node_yes_child, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "no_child", mrb_naraku_node_no_child, MRB_ARGS_NONE());
  mrb_define_method(mrb, node_class, "children", mrb_naraku_node_children, MRB_ARGS_NONE());

  // `Naraku::Node::Root` (internal, not for public use):
  struct RClass* node_root_class = mrb_define_class_under(mrb, node_class, "Root", mrb->object_class);
  MRB_SET_INSTANCE_TT(node_root_class, MRB_TT_DATA);
  MRB_UNDEF_ALLOCATOR(node_root_class);
  mrb_undef_class_method_id(mrb, node_root_class, MRB_SYM(new));
  mrb_undef_class_method_id(mrb, node_root_class, MRB_SYM(allocate));

  // `Naraku::Node::CharClassUnion`:
  struct RClass* cc_union_class = mrb_define_class_under(mrb, node_class, "CharClassUnion", mrb->object_class);
  MRB_SET_INSTANCE_TT(cc_union_class, MRB_TT_DATA);
  MRB_UNDEF_ALLOCATOR(cc_union_class);
  mrb_undef_class_method_id(mrb, cc_union_class, MRB_SYM(new));
  mrb_undef_class_method_id(mrb, cc_union_class, MRB_SYM(allocate));

  mrb_define_method(mrb, cc_union_class, "items", mrb_naraku_char_class_union_items, MRB_ARGS_NONE());

  // `Naraku::Node::CharClassItem`:
  struct RClass* cc_item_class = mrb_define_class_under(mrb, node_class, "CharClassItem", mrb->object_class);
  MRB_SET_INSTANCE_TT(cc_item_class, MRB_TT_DATA);
  MRB_UNDEF_ALLOCATOR(cc_item_class);
  mrb_undef_class_method_id(mrb, cc_item_class, MRB_SYM(new));
  mrb_undef_class_method_id(mrb, cc_item_class, MRB_SYM(allocate));

  mrb_define_method(mrb, cc_item_class, "type", mrb_naraku_char_class_item_type, MRB_ARGS_NONE());
  mrb_define_method(mrb, cc_item_class, "code", mrb_naraku_char_class_item_code, MRB_ARGS_NONE());
  mrb_define_method(mrb, cc_item_class, "from_code", mrb_naraku_char_class_item_from_code, MRB_ARGS_NONE());
  mrb_define_method(mrb, cc_item_class, "to_code", mrb_naraku_char_class_item_to_code, MRB_ARGS_NONE());
  mrb_define_method(mrb, cc_item_class, "is_positive", mrb_naraku_char_class_item_is_positive, MRB_ARGS_NONE());
  mrb_define_method(mrb, cc_item_class, "is_ascii_only", mrb_naraku_char_class_item_is_ascii_only, MRB_ARGS_NONE());
  mrb_define_method(mrb, cc_item_class, "char_type", mrb_naraku_char_class_item_char_type, MRB_ARGS_NONE());
  mrb_define_method(
    mrb,
    cc_item_class,
    "posix_char_class",
    mrb_naraku_char_class_item_posix_char_class,
    MRB_ARGS_NONE()
  );
  mrb_define_method(mrb, cc_item_class, "cprop", mrb_naraku_char_class_item_cprop, MRB_ARGS_NONE());
  mrb_define_method(mrb, cc_item_class, "unions", mrb_naraku_char_class_item_unions, MRB_ARGS_NONE());
}
