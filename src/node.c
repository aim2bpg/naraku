#include <naraku_syntax.h>
#include <naraku_syntax_internal.h>

#include <stdlib.h> // for malloc, free
#include <string.h> // for memcpy

nk_error_t pbuf_concat(
    const nk_pbuf_t* buf1,
    const nk_pbuf_t* buf2,
    nk_pbuf_t* out_buf
) {
  // Special case: if both buffers are views and they are contiguous, we can create a new view that spans both buffers without copying.
  if (buf1->type == NK_PBUF_VIEW && buf2->type == NK_PBUF_VIEW && buf1->bytes_end == buf2->bytes) {
    out_buf->type = NK_PBUF_VIEW;
    out_buf->bytes = buf1->bytes;
    out_buf->bytes_end = buf2->bytes_end;
    return NK_SUCCESS;
  }

  // General case: we need to allocate a new buffer and copy the contents of both buffers into it.

  size_t len1 = (size_t)(buf1->bytes_end - buf1->bytes);
  size_t len2 = (size_t)(buf2->bytes_end - buf2->bytes);
  size_t total_len = len1 + len2;

  uint8_t* new_bytes = (uint8_t*)malloc(total_len);
  if (new_bytes == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  memcpy(new_bytes, buf1->bytes, len1);
  memcpy(new_bytes + len1, buf2->bytes, len2);

  out_buf->type = NK_PBUF_OWNED;
  out_buf->bytes = new_bytes;
  out_buf->bytes_end = new_bytes + total_len;

  return NK_SUCCESS;
}

void nk_pbuf_free(nk_pbuf_t* pbuf) {
  if (pbuf == NULL || pbuf->type == NK_PBUF_VIEW) {
    return;
  }

  free((void*)pbuf->bytes);
  pbuf->bytes = NULL;
  pbuf->bytes_end = NULL;
}

static void char_class_union_free(nk_char_class_union_t* u);

void nodes_free(nk_node_t** nodes, size_t len) {
  if (nodes == NULL) {
    return;
  }

  for (size_t i = 0; i < len; i++) {
    nk_node_free(nodes[i]);
  }

  free(nodes);
}

void nk_node_free(nk_node_t* node) {
  if (node == NULL) {
    return;
  }

  switch (node->base.type) {
    case NK_NODE_TYPE_UNKNOWN:
      break;
    case NK_NODE_TYPE_LITERAL:
      nk_pbuf_free(&node->literal.buf);
      break;
    case NK_NODE_TYPE_CHAR_CLASS:
      if (node->char_class.unions != NULL) {
        for (size_t i = 0; i < node->char_class.unions_len; i++) {
          char_class_union_free(node->char_class.unions[i]);
        }
        free(node->char_class.unions);
      }
      node->char_class.unions = NULL;
      break;
    case NK_NODE_TYPE_CHAR_TYPE:
    case NK_NODE_TYPE_CHAR_PROP:
    case NK_NODE_TYPE_DOT:
    case NK_NODE_TYPE_NEWLINE:
    case NK_NODE_TYPE_GRAPHEME_CLUSTER:
    case NK_NODE_TYPE_KEEP:
      break;
    case NK_NODE_TYPE_BACK_REF:
      if (node->back_ref.has_name) {
        nk_pbuf_free(&node->back_ref.name_buf);
      }
      break;
    case NK_NODE_TYPE_CALL:
      if (node->call.has_name) {
        nk_pbuf_free(&node->call.name_buf);
      }
      break;
    case NK_NODE_TYPE_ASSERTION:
      if (node->assertion.child != NULL) {
        nk_node_free(node->assertion.child);
        node->assertion.child = NULL;
      }
      break;
    case NK_NODE_TYPE_QUANTIFIER:
      if (node->quantifier.child != NULL) {
        nk_node_free(node->quantifier.child);
        node->quantifier.child = NULL;
      }
      break;
    case NK_NODE_TYPE_GROUP:
      if (node->group.has_name) {
        nk_pbuf_free(&node->group.name_buf);
      }
      if (node->group.child != NULL) {
        nk_node_free(node->group.child);
        node->group.child = NULL;
      }
      break;
    case NK_NODE_TYPE_ATOMIC:
      if (node->atomic.child != NULL) {
        nk_node_free(node->atomic.child);
        node->atomic.child = NULL;
      }
      break;
    case NK_NODE_TYPE_CONDITIONAL:
      if (node->conditional.has_name) {
        nk_pbuf_free(&node->conditional.name_buf);
      }
      if (node->conditional.yes_child != NULL) {
        nk_node_free(node->conditional.yes_child);
        node->conditional.yes_child = NULL;
      }
      if (node->conditional.no_child != NULL) {
        nk_node_free(node->conditional.no_child);
        node->conditional.no_child = NULL;
      }
      break;
    case NK_NODE_TYPE_CONCAT:
      nodes_free(node->concat.children, node->concat.children_len);
      node->concat.children = NULL;
      break;
    case NK_NODE_TYPE_ALT:
      nodes_free(node->alt.children, node->alt.children_len);
      node->alt.children = NULL;
      break;
  }

  node->base.type = NK_NODE_TYPE_UNKNOWN;
}

static void char_class_item_free(nk_char_class_item_t* item) {
  if (item == NULL) {
    return;
  }

  switch (item->type) {
    case NK_CHAR_CLASS_ITEM_TYPE_CODE:
    case NK_CHAR_CLASS_ITEM_TYPE_RANGE:
    case NK_CHAR_CLASS_ITEM_TYPE_CHAR_TYPE:
    case NK_CHAR_CLASS_ITEM_TYPE_CHAR_PROP:
    case NK_CHAR_CLASS_ITEM_TYPE_POSIX_CHAR_CLASS:
      break;
    case NK_CHAR_CLASS_ITEM_TYPE_NESTED_CHAR_CLASS:
      if (item->data.nested_char_class.unions != NULL) {
        for (size_t i = 0; i < item->data.nested_char_class.unions_len; i++) {
          char_class_union_free(item->data.nested_char_class.unions[i]);
        }
        free(item->data.nested_char_class.unions);
        item->data.nested_char_class.unions = NULL;
      }
      break;
  }
}

static void char_class_union_free(nk_char_class_union_t* u) {
  if (u == NULL) {
    return;
  }

  if (u->items != NULL) {
    for (size_t i = 0; i < u->items_len; i++) {
      char_class_item_free(u->items[i]);
    }
    free(u->items);
    u->items = NULL;
  }
}
