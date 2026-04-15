#include <naraku_syntax.h>
#include <naraku_syntax_internal.h>

#include <stdlib.h>  // for malloc, free
#include <string.h>  // for memcpy

nk_error_t pbuf_append(nk_pbuf_t* buf1, const nk_pbuf_t* buf2) {
  // Special case: if both buffers are views and they are contiguous, we can
  // create a new view that spans both buffers without copying.
  if (buf1->type == NK_PBUF_VIEW && buf2->type == NK_PBUF_VIEW && buf1->bytes_end == buf2->bytes) {
    buf1->type = NK_PBUF_VIEW;
    buf1->bytes = buf1->bytes;
    buf1->bytes_end = buf2->bytes_end;
    return NK_SUCCESS;
  }

  size_t len1 = (size_t)(buf1->bytes_end - buf1->bytes);
  size_t len2 = (size_t)(buf2->bytes_end - buf2->bytes);
  size_t new_len = len1 + len2;

  if (buf1->type == NK_PBUF_OWNED && buf1->cap >= new_len) {
    // If `buf1` is already an owned buffer with enough capacity, we can append
    // `buf2` to it without reallocating.
    memcpy((uint8_t*)buf1->bytes + len1, buf2->bytes, len2);
    buf1->bytes_end = buf1->bytes + new_len;
    return NK_SUCCESS;
  }

  // Otherwise, we need to allocate a new owned buffer and copy both `buf1` and `buf2` into it.
  size_t new_cap = 1;
  while (new_cap < new_len) {
    new_cap *= 2;
  }
  uint8_t* new_bytes = (uint8_t*)malloc(new_cap);
  if (new_bytes == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  memcpy(new_bytes, buf1->bytes, len1);
  memcpy(new_bytes + len1, buf2->bytes, len2);

  if (buf1->type == NK_PBUF_OWNED) {
    free((void*)buf1->bytes);
  }

  buf1->type = NK_PBUF_OWNED;
  buf1->cap = new_cap;
  buf1->bytes = new_bytes;
  buf1->bytes_end = new_bytes + new_len;

  return NK_SUCCESS;
}

nk_error_t pbuf_append_code(nk_pbuf_t* buf, const nk_encoding_t* enc, uint32_t code) {
  uint8_t mbc_bytes[NK_ENC_MAX_MBC_WIDTH];
  size_t mbc_width;
  nk_error_t err = nk_enc_encode_mbc(enc, code, &mbc_width, mbc_bytes);
  if (err != NK_SUCCESS) {
    return err;
  }

  nk_pbuf_t mbc_pbuf = {.type = NK_PBUF_VIEW, .bytes = mbc_bytes, .bytes_end = mbc_bytes + mbc_width};
  return pbuf_append(buf, &mbc_pbuf);
}

nk_error_t pbuf_resize(nk_pbuf_t* buf) {
  if (buf->type == NK_PBUF_VIEW) {
    return NK_SUCCESS;
  }

  size_t len = (size_t)(buf->bytes_end - buf->bytes);
  uint8_t* new_bytes = (uint8_t*)realloc((void*)buf->bytes, len);
  if (new_bytes == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  buf->bytes = new_bytes;
  buf->bytes_end = new_bytes + len;
  buf->cap = len;

  return NK_SUCCESS;
}

void nk_pbuf_free(nk_pbuf_t* pbuf) {
  if (pbuf == NULL || pbuf->type == NK_PBUF_VIEW) {
    return;
  }

  free((void*)pbuf->bytes);
  pbuf->bytes = NULL;
  pbuf->bytes_end = NULL;
  pbuf->cap = 0;
}

nk_error_t nk_pbuf_to_owned(nk_pbuf_t* pbuf) {
  if (pbuf == NULL || pbuf->type == NK_PBUF_OWNED) {
    return NK_SUCCESS;
  }

  size_t len = (size_t)(pbuf->bytes_end - pbuf->bytes);
  uint8_t* copy = (uint8_t*)malloc(len);
  if (copy == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  memcpy(copy, pbuf->bytes, len);
  pbuf->type = NK_PBUF_OWNED;
  pbuf->cap = len;
  pbuf->bytes = copy;
  pbuf->bytes_end = copy + len;
  return NK_SUCCESS;
}

void char_class_union_free(nk_char_class_union_t* u);

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
      if (node->back_ref.target_kind == NK_REF_TARGET_KIND_NAME) {
        nk_pbuf_free(&node->back_ref.name_buf);
      }
      break;
    case NK_NODE_TYPE_CALL:
      if (node->call.target_kind == NK_CALL_TARGET_KIND_NAME) {
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
    case NK_NODE_TYPE_CAPTURE:
      if (node->capture.has_name) {
        nk_pbuf_free(&node->capture.name_buf);
      }
      if (node->capture.child != NULL) {
        nk_node_free(node->capture.child);
        node->capture.child = NULL;
      }
      break;
    case NK_NODE_TYPE_GROUP:
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
    case NK_NODE_TYPE_ABSENCE:
      if (node->absence.child != NULL) {
        nk_node_free(node->absence.child);
        node->absence.child = NULL;
      }
      break;
    case NK_NODE_TYPE_CONDITIONAL:
      if (node->conditional.target_kind == NK_REF_TARGET_KIND_NAME) {
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

  free(node);
}

void char_class_item_free(nk_char_class_item_t* item) {
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

  free(item);
}

void char_class_union_free(nk_char_class_union_t* u) {
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

  free(u);
}

nk_error_t nk_node_to_owned(nk_node_t* node) {
  if (node == NULL) {
    return NK_SUCCESS;
  }

  nk_error_t err;
  switch (node->base.type) {
    case NK_NODE_TYPE_UNKNOWN:
    case NK_NODE_TYPE_DOT:
    case NK_NODE_TYPE_NEWLINE:
    case NK_NODE_TYPE_GRAPHEME_CLUSTER:
    case NK_NODE_TYPE_KEEP:
    case NK_NODE_TYPE_CHAR_CLASS:
    case NK_NODE_TYPE_CHAR_TYPE:
    case NK_NODE_TYPE_CHAR_PROP:
      break;
    case NK_NODE_TYPE_LITERAL:
      err = nk_pbuf_to_owned(&node->literal.buf);
      if (err != NK_SUCCESS) {
        return err;
      }
      break;
    case NK_NODE_TYPE_BACK_REF:
      if (node->back_ref.target_kind == NK_REF_TARGET_KIND_NAME) {
        err = nk_pbuf_to_owned(&node->back_ref.name_buf);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      break;
    case NK_NODE_TYPE_CALL:
      if (node->call.target_kind == NK_CALL_TARGET_KIND_NAME) {
        err = nk_pbuf_to_owned(&node->call.name_buf);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      break;
    case NK_NODE_TYPE_ASSERTION:
      err = nk_node_to_owned(node->assertion.child);
      if (err != NK_SUCCESS) {
        return err;
      }
      break;
    case NK_NODE_TYPE_QUANTIFIER:
      err = nk_node_to_owned(node->quantifier.child);
      if (err != NK_SUCCESS) {
        return err;
      }
      break;
    case NK_NODE_TYPE_CAPTURE:
      if (node->capture.has_name) {
        err = nk_pbuf_to_owned(&node->capture.name_buf);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      err = nk_node_to_owned(node->capture.child);
      if (err != NK_SUCCESS) {
        return err;
      }
      break;
    case NK_NODE_TYPE_GROUP:
      err = nk_node_to_owned(node->group.child);
      if (err != NK_SUCCESS) {
        return err;
      }
      break;
    case NK_NODE_TYPE_ATOMIC:
      err = nk_node_to_owned(node->atomic.child);
      if (err != NK_SUCCESS) {
        return err;
      }
      break;
    case NK_NODE_TYPE_ABSENCE:
      err = nk_node_to_owned(node->absence.child);
      if (err != NK_SUCCESS) {
        return err;
      }
      break;
    case NK_NODE_TYPE_CONDITIONAL:
      if (node->conditional.target_kind == NK_REF_TARGET_KIND_NAME) {
        err = nk_pbuf_to_owned(&node->conditional.name_buf);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      err = nk_node_to_owned(node->conditional.yes_child);
      if (err != NK_SUCCESS) {
        return err;
      }
      err = nk_node_to_owned(node->conditional.no_child);
      if (err != NK_SUCCESS) {
        return err;
      }
      break;
    case NK_NODE_TYPE_CONCAT:
      for (size_t i = 0; i < node->concat.children_len; i++) {
        err = nk_node_to_owned(node->concat.children[i]);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      break;
    case NK_NODE_TYPE_ALT:
      for (size_t i = 0; i < node->alt.children_len; i++) {
        err = nk_node_to_owned(node->alt.children[i]);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      break;
  }

  return NK_SUCCESS;
}
