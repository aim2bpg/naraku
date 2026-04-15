#include <naraku_syntax.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static nk_error_t set_capture_num_impl(nk_node_t* node, uint32_t* next_capture_num) {
  if (node == NULL) {
    return NK_SUCCESS;
  }

  switch (node->base.type) {
    case NK_NODE_TYPE_UNKNOWN:
    case NK_NODE_TYPE_LITERAL:
    case NK_NODE_TYPE_CHAR_CLASS:
    case NK_NODE_TYPE_CHAR_TYPE:
    case NK_NODE_TYPE_CHAR_PROP:
    case NK_NODE_TYPE_DOT:
    case NK_NODE_TYPE_NEWLINE:
    case NK_NODE_TYPE_GRAPHEME_CLUSTER:
    case NK_NODE_TYPE_KEEP:
    case NK_NODE_TYPE_BACK_REF:
    case NK_NODE_TYPE_CALL:
      return NK_SUCCESS;
    case NK_NODE_TYPE_ASSERTION:
      return set_capture_num_impl(node->assertion.child, next_capture_num);
    case NK_NODE_TYPE_QUANTIFIER:
      return set_capture_num_impl(node->quantifier.child, next_capture_num);
    case NK_NODE_TYPE_CAPTURE:
      if (node->capture.has_name) {
        node->capture.capture_num = *next_capture_num;
        *next_capture_num += 1;
        return set_capture_num_impl(node->capture.child, next_capture_num);
      } else {
        nk_node_t* child = node->capture.child;
        node->base.type = NK_NODE_TYPE_GROUP;
        node->group.child = child;
        return set_capture_num_impl(node->group.child, next_capture_num);
      }
    case NK_NODE_TYPE_GROUP:
      return set_capture_num_impl(node->group.child, next_capture_num);
    case NK_NODE_TYPE_ATOMIC:
      return set_capture_num_impl(node->atomic.child, next_capture_num);
    case NK_NODE_TYPE_ABSENCE:
      return set_capture_num_impl(node->absence.child, next_capture_num);
    case NK_NODE_TYPE_CONDITIONAL:
    {
      nk_error_t err = set_capture_num_impl(node->conditional.yes_child, next_capture_num);
      if (err != NK_SUCCESS) {
        return err;
      }
      return set_capture_num_impl(node->conditional.no_child, next_capture_num);
    }
    case NK_NODE_TYPE_CONCAT:
      for (size_t i = 0; i < node->concat.children_len; i++) {
        nk_error_t err = set_capture_num_impl(node->concat.children[i], next_capture_num);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      return NK_SUCCESS;
    case NK_NODE_TYPE_ALT:
      for (size_t i = 0; i < node->alt.children_len; i++) {
        nk_error_t err = set_capture_num_impl(node->alt.children[i], next_capture_num);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      return NK_SUCCESS;
  }

  return NK_ERR_PARSER_BUG;
}

static nk_error_t set_capture_num(nk_parser_t* parser, nk_node_t* root_node) {
  if (!parser->has_named_captures || root_node == NULL) {
    return NK_SUCCESS;
  }

  uint32_t next_capture_num = 1;
  nk_error_t err = set_capture_num_impl(root_node, &next_capture_num);
  if (err != NK_SUCCESS) {
    return err;
  }

  parser->num_capture_groups = next_capture_num - 1;
  return NK_SUCCESS;
}

static inline void set_error_span_from_node(nk_parser_t* parser, nk_node_t* node) {
  parser->error_bytes = parser->pattern_bytes_begin + node->base.span_offset;
  parser->error_bytes_end = parser->error_bytes + node->base.span_length;
}

static inline void report_warning_from_node(nk_parser_t* parser, nk_warning_t warning, nk_node_t* node) {
  if (parser->warning_func == NULL) {
    return;
  }

  parser->warning_func(parser, warning, node->base.span_offset, node->base.span_length);
}

static void clear_capture_maps(nk_parser_t* parser) {
  if (parser->capture_nodes_by_num != NULL) {
    free(parser->capture_nodes_by_num);
    parser->capture_nodes_by_num = NULL;
    parser->capture_nodes_by_num_len = 0;
  }
  if (parser->capture_entry_index_by_num != NULL) {
    free(parser->capture_entry_index_by_num);
    parser->capture_entry_index_by_num = NULL;
    parser->capture_entry_index_by_num_len = 0;
  }

  if (parser->capture_name_map.entries != NULL) {
    for (size_t i = 0; i < parser->capture_name_map.entries_len; i++) {
      nk_capture_name_map_entry_t* entry = &parser->capture_name_map.entries[i];
      if (entry->has_name) {
        nk_pbuf_free(&entry->name_buf);
      }
      free(entry->capture_nums);
    }
    free(parser->capture_name_map.entries);
    parser->capture_name_map.entries = NULL;
    parser->capture_name_map.entries_len = 0;
    parser->capture_name_map.entries_cap = 0;
  }
}

static bool pbuf_equal(const nk_pbuf_t* left, const nk_pbuf_t* right) {
  size_t left_len = (size_t)(left->bytes_end - left->bytes);
  size_t right_len = (size_t)(right->bytes_end - right->bytes);
  if (left_len != right_len) {
    return false;
  }
  return memcmp(left->bytes, right->bytes, left_len) == 0;
}

static nk_error_t append_capture_num(nk_capture_name_map_entry_t* entry, uint32_t capture_num) {
  if (entry->capture_nums_len >= entry->capture_nums_cap) {
    size_t new_cap = entry->capture_nums_cap == 0 ? 4 : entry->capture_nums_cap * 2;
    uint32_t* new_capture_nums = (uint32_t*)realloc(entry->capture_nums, sizeof(uint32_t) * new_cap);
    if (new_capture_nums == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    entry->capture_nums = new_capture_nums;
    entry->capture_nums_cap = new_cap;
  }
  entry->capture_nums[entry->capture_nums_len++] = capture_num;
  return NK_SUCCESS;
}

static nk_error_t
get_or_add_capture_name_entry(nk_capture_name_map_t* map, const nk_pbuf_t* name_buf, size_t* out_entry_index) {
  for (size_t i = 0; i < map->entries_len; i++) {
    if (map->entries[i].has_name && pbuf_equal(&map->entries[i].name_buf, name_buf)) {
      *out_entry_index = i;
      return NK_SUCCESS;
    }
  }

  if (map->entries_len >= map->entries_cap) {
    size_t new_cap = map->entries_cap == 0 ? 4 : map->entries_cap * 2;
    nk_capture_name_map_entry_t* new_entries =
      (nk_capture_name_map_entry_t*)realloc(map->entries, sizeof(nk_capture_name_map_entry_t) * new_cap);
    if (new_entries == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    map->entries = new_entries;
    map->entries_cap = new_cap;
  }

  nk_capture_name_map_entry_t* entry = &map->entries[map->entries_len];
  entry->has_name = true;
  entry->capture_nums = NULL;
  entry->capture_nums_len = 0;
  entry->capture_nums_cap = 0;
  size_t name_len = (size_t)(name_buf->bytes_end - name_buf->bytes);
  uint8_t* bytes = (uint8_t*)malloc(name_len);
  if (bytes == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  memcpy(bytes, name_buf->bytes, name_len);
  entry->name_buf = (nk_pbuf_t){.type = NK_PBUF_OWNED, .cap = name_len, .bytes = bytes, .bytes_end = bytes + name_len};

  *out_entry_index = map->entries_len;
  map->entries_len++;
  return NK_SUCCESS;
}

static nk_error_t add_numeric_capture_entry(nk_capture_name_map_t* map, uint32_t capture_num, size_t* out_entry_index) {
  if (map->entries_len >= map->entries_cap) {
    size_t new_cap = map->entries_cap == 0 ? 4 : map->entries_cap * 2;
    nk_capture_name_map_entry_t* new_entries =
      (nk_capture_name_map_entry_t*)realloc(map->entries, sizeof(nk_capture_name_map_entry_t) * new_cap);
    if (new_entries == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    map->entries = new_entries;
    map->entries_cap = new_cap;
  }

  nk_capture_name_map_entry_t* entry = &map->entries[map->entries_len];
  entry->has_name = false;
  entry->name_buf = (nk_pbuf_t){0};
  entry->capture_nums = NULL;
  entry->capture_nums_len = 0;
  entry->capture_nums_cap = 0;
  nk_error_t err = append_capture_num(entry, capture_num);
  if (err != NK_SUCCESS) {
    return err;
  }

  *out_entry_index = map->entries_len;
  map->entries_len++;
  return NK_SUCCESS;
}

static nk_error_t build_capture_maps_impl(nk_parser_t* parser, nk_node_t* node) {
  if (node == NULL) {
    return NK_SUCCESS;
  }

  switch (node->base.type) {
    case NK_NODE_TYPE_UNKNOWN:
    case NK_NODE_TYPE_LITERAL:
    case NK_NODE_TYPE_CHAR_CLASS:
    case NK_NODE_TYPE_CHAR_TYPE:
    case NK_NODE_TYPE_CHAR_PROP:
    case NK_NODE_TYPE_DOT:
    case NK_NODE_TYPE_NEWLINE:
    case NK_NODE_TYPE_GRAPHEME_CLUSTER:
    case NK_NODE_TYPE_KEEP:
    case NK_NODE_TYPE_BACK_REF:
    case NK_NODE_TYPE_CALL:
      return NK_SUCCESS;
    case NK_NODE_TYPE_ASSERTION:
      return build_capture_maps_impl(parser, node->assertion.child);
    case NK_NODE_TYPE_QUANTIFIER:
      return build_capture_maps_impl(parser, node->quantifier.child);
    case NK_NODE_TYPE_CAPTURE:
    {
      uint32_t capture_num = node->capture.capture_num;
      if (capture_num > 0 && capture_num < parser->capture_nodes_by_num_len) {
        parser->capture_nodes_by_num[capture_num] = node;
      }

      if (parser->has_named_captures && node->capture.has_name) {
        size_t entry_index = SIZE_MAX;
        nk_error_t err =
          get_or_add_capture_name_entry(&parser->capture_name_map, &node->capture.name_buf, &entry_index);
        if (err != NK_SUCCESS) {
          return err;
        }
        err = append_capture_num(&parser->capture_name_map.entries[entry_index], capture_num);
        if (err != NK_SUCCESS) {
          return err;
        }
        if (capture_num < parser->capture_entry_index_by_num_len) {
          parser->capture_entry_index_by_num[capture_num] = entry_index;
        }
      }
      return build_capture_maps_impl(parser, node->capture.child);
    }
    case NK_NODE_TYPE_GROUP:
      return build_capture_maps_impl(parser, node->group.child);
    case NK_NODE_TYPE_ATOMIC:
      return build_capture_maps_impl(parser, node->atomic.child);
    case NK_NODE_TYPE_ABSENCE:
      return build_capture_maps_impl(parser, node->absence.child);
    case NK_NODE_TYPE_CONDITIONAL:
    {
      nk_error_t err = build_capture_maps_impl(parser, node->conditional.yes_child);
      if (err != NK_SUCCESS) {
        return err;
      }
      return build_capture_maps_impl(parser, node->conditional.no_child);
    }
    case NK_NODE_TYPE_CONCAT:
      for (size_t i = 0; i < node->concat.children_len; i++) {
        nk_error_t err = build_capture_maps_impl(parser, node->concat.children[i]);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      return NK_SUCCESS;
    case NK_NODE_TYPE_ALT:
      for (size_t i = 0; i < node->alt.children_len; i++) {
        nk_error_t err = build_capture_maps_impl(parser, node->alt.children[i]);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      return NK_SUCCESS;
  }

  return NK_ERR_PARSER_BUG;
}

static nk_error_t build_capture_maps(nk_parser_t* parser, nk_node_t* root_node) {
  if (root_node == NULL) {
    return NK_SUCCESS;
  }

  size_t capture_nodes_len = (size_t)parser->num_capture_groups + 1;
  parser->capture_nodes_by_num = (nk_node_t**)calloc(capture_nodes_len, sizeof(nk_node_t*));
  if (parser->capture_nodes_by_num == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  parser->capture_nodes_by_num_len = capture_nodes_len;
  parser->capture_entry_index_by_num = (size_t*)malloc(sizeof(size_t) * capture_nodes_len);
  if (parser->capture_entry_index_by_num == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  parser->capture_entry_index_by_num_len = capture_nodes_len;
  for (size_t i = 0; i < capture_nodes_len; i++) {
    parser->capture_entry_index_by_num[i] = SIZE_MAX;
  }

  nk_error_t err = build_capture_maps_impl(parser, root_node);
  if (err != NK_SUCCESS) {
    return err;
  }

  if (!parser->has_named_captures) {
    for (uint32_t capture_num = 1; capture_num <= parser->num_capture_groups; capture_num++) {
      if (capture_num >= parser->capture_nodes_by_num_len || parser->capture_nodes_by_num[capture_num] == NULL) {
        continue;
      }
      size_t entry_index = SIZE_MAX;
      err = add_numeric_capture_entry(&parser->capture_name_map, capture_num, &entry_index);
      if (err != NK_SUCCESS) {
        return err;
      }
      parser->capture_entry_index_by_num[capture_num] = entry_index;
    }
  }

  return NK_SUCCESS;
}

static nk_error_t
find_capture_name_entry_index(const nk_capture_name_map_t* map, const nk_pbuf_t* name_buf, size_t* out_index) {
  for (size_t i = 0; i < map->entries_len; i++) {
    if (map->entries[i].has_name && pbuf_equal(&map->entries[i].name_buf, name_buf)) {
      *out_index = i;
      return NK_SUCCESS;
    }
  }
  return NK_ERR_UNDEFINED_BACK_REF;
}

static void clear_resolved_refs_impl(nk_node_t* node) {
  if (node == NULL) {
    return;
  }

  switch (node->base.type) {
    case NK_NODE_TYPE_UNKNOWN:
    case NK_NODE_TYPE_LITERAL:
    case NK_NODE_TYPE_CHAR_CLASS:
    case NK_NODE_TYPE_CHAR_TYPE:
    case NK_NODE_TYPE_CHAR_PROP:
    case NK_NODE_TYPE_DOT:
    case NK_NODE_TYPE_NEWLINE:
    case NK_NODE_TYPE_GRAPHEME_CLUSTER:
    case NK_NODE_TYPE_KEEP:
      return;
    case NK_NODE_TYPE_BACK_REF:
      node->back_ref.resolved_capture_name_map_entry_index = SIZE_MAX;
      node->back_ref.resolved_capture_num_count = 0;
      return;
    case NK_NODE_TYPE_CALL:
      node->call.resolved_capture_num = 0;
      return;
    case NK_NODE_TYPE_ASSERTION:
      clear_resolved_refs_impl(node->assertion.child);
      return;
    case NK_NODE_TYPE_QUANTIFIER:
      clear_resolved_refs_impl(node->quantifier.child);
      return;
    case NK_NODE_TYPE_CAPTURE:
      clear_resolved_refs_impl(node->capture.child);
      return;
    case NK_NODE_TYPE_GROUP:
      clear_resolved_refs_impl(node->group.child);
      return;
    case NK_NODE_TYPE_ATOMIC:
      clear_resolved_refs_impl(node->atomic.child);
      return;
    case NK_NODE_TYPE_ABSENCE:
      clear_resolved_refs_impl(node->absence.child);
      return;
    case NK_NODE_TYPE_CONDITIONAL:
      node->conditional.resolved_capture_name_map_entry_index = SIZE_MAX;
      node->conditional.resolved_capture_num_count = 0;
      clear_resolved_refs_impl(node->conditional.yes_child);
      clear_resolved_refs_impl(node->conditional.no_child);
      return;
    case NK_NODE_TYPE_CONCAT:
      for (size_t i = 0; i < node->concat.children_len; i++) {
        clear_resolved_refs_impl(node->concat.children[i]);
      }
      return;
    case NK_NODE_TYPE_ALT:
      for (size_t i = 0; i < node->alt.children_len; i++) {
        clear_resolved_refs_impl(node->alt.children[i]);
      }
      return;
  }
}

static nk_error_t resolve_back_ref(nk_parser_t* parser, nk_node_t* node, const size_t* named_seen_counts) {
  if (parser->has_named_captures) {
    if (node->back_ref.target_kind != NK_REF_TARGET_KIND_NAME) {
      set_error_span_from_node(parser, node);
      return NK_ERR_INVALID_BACK_REF;
    }
    size_t entry_index = SIZE_MAX;
    if (
      find_capture_name_entry_index(&parser->capture_name_map, &node->back_ref.name_buf, &entry_index) != NK_SUCCESS
    ) {
      set_error_span_from_node(parser, node);
      return NK_ERR_UNDEFINED_BACK_REF;
    }
    size_t resolved_count = named_seen_counts == NULL ? 0 : named_seen_counts[entry_index];
    if (resolved_count == 0) {
      set_error_span_from_node(parser, node);
      return NK_ERR_UNDEFINED_BACK_REF;
    }
    if (resolved_count < parser->capture_name_map.entries[entry_index].capture_nums_len) {
      report_warning_from_node(parser, NK_WARN_NAMED_GROUP_DEFINED_AFTER_REFERENCE, node);
    }
    node->back_ref.resolved_capture_name_map_entry_index = entry_index;
    node->back_ref.resolved_capture_num_count = resolved_count;
    return NK_SUCCESS;
  }

  if (node->back_ref.target_kind != NK_REF_TARGET_KIND_CAPTURE_NUM) {
    set_error_span_from_node(parser, node);
    return NK_ERR_INVALID_BACK_REF;
  }
  uint32_t capture_num = node->back_ref.capture_num;
  if (
    capture_num == 0 || capture_num >= parser->capture_nodes_by_num_len ||
    parser->capture_nodes_by_num[capture_num] == NULL || capture_num >= parser->capture_entry_index_by_num_len ||
    parser->capture_entry_index_by_num[capture_num] == SIZE_MAX
  ) {
    set_error_span_from_node(parser, node);
    return NK_ERR_UNDEFINED_BACK_REF;
  }

  node->back_ref.resolved_capture_name_map_entry_index = parser->capture_entry_index_by_num[capture_num];
  node->back_ref.resolved_capture_num_count = 1;
  return NK_SUCCESS;
}

static nk_error_t resolve_conditional(nk_parser_t* parser, nk_node_t* node, const size_t* named_seen_counts) {
  if (parser->has_named_captures) {
    if (node->conditional.target_kind != NK_REF_TARGET_KIND_NAME) {
      set_error_span_from_node(parser, node);
      return NK_ERR_INVALID_CONDITIONAL_GROUP;
    }
    size_t entry_index = SIZE_MAX;
    if (
      find_capture_name_entry_index(&parser->capture_name_map, &node->conditional.name_buf, &entry_index) != NK_SUCCESS
    ) {
      set_error_span_from_node(parser, node);
      return NK_ERR_UNDEFINED_CONDITIONAL_REF;
    }
    size_t resolved_count = named_seen_counts == NULL ? 0 : named_seen_counts[entry_index];
    if (resolved_count == 0) {
      set_error_span_from_node(parser, node);
      return NK_ERR_UNDEFINED_CONDITIONAL_REF;
    }
    if (resolved_count < parser->capture_name_map.entries[entry_index].capture_nums_len) {
      report_warning_from_node(parser, NK_WARN_NAMED_GROUP_DEFINED_AFTER_REFERENCE, node);
    }
    node->conditional.resolved_capture_name_map_entry_index = entry_index;
    node->conditional.resolved_capture_num_count = resolved_count;
    return NK_SUCCESS;
  }

  if (node->conditional.target_kind != NK_REF_TARGET_KIND_CAPTURE_NUM) {
    set_error_span_from_node(parser, node);
    return NK_ERR_INVALID_CONDITIONAL_GROUP;
  }
  uint32_t capture_num = node->conditional.capture_num;
  if (
    capture_num == 0 || capture_num >= parser->capture_nodes_by_num_len ||
    parser->capture_nodes_by_num[capture_num] == NULL || capture_num >= parser->capture_entry_index_by_num_len ||
    parser->capture_entry_index_by_num[capture_num] == SIZE_MAX
  ) {
    set_error_span_from_node(parser, node);
    return NK_ERR_UNDEFINED_CONDITIONAL_REF;
  }

  node->conditional.resolved_capture_name_map_entry_index = parser->capture_entry_index_by_num[capture_num];
  node->conditional.resolved_capture_num_count = 1;
  return NK_SUCCESS;
}

static nk_error_t resolve_call(nk_parser_t* parser, nk_node_t* node) {
  if (node->call.target_kind == NK_CALL_TARGET_KIND_ROOT) {
    node->call.resolved_capture_num = 0;
    return NK_SUCCESS;
  }

  if (parser->has_named_captures) {
    if (node->call.target_kind != NK_CALL_TARGET_KIND_NAME) {
      set_error_span_from_node(parser, node);
      return NK_ERR_INVALID_SUBEXP_CALL;
    }
    size_t entry_index = SIZE_MAX;
    if (find_capture_name_entry_index(&parser->capture_name_map, &node->call.name_buf, &entry_index) != NK_SUCCESS) {
      set_error_span_from_node(parser, node);
      return NK_ERR_UNDEFINED_SUBEXP_CALL;
    }
    if (parser->capture_name_map.entries[entry_index].capture_nums_len != 1) {
      set_error_span_from_node(parser, node);
      return NK_ERR_INVALID_SUBEXP_CALL;
    }
    node->call.resolved_capture_num = parser->capture_name_map.entries[entry_index].capture_nums[0];
    return NK_SUCCESS;
  }

  if (node->call.target_kind != NK_CALL_TARGET_KIND_CAPTURE_NUM) {
    set_error_span_from_node(parser, node);
    return NK_ERR_INVALID_SUBEXP_CALL;
  }
  uint32_t capture_num = node->call.capture_num;
  if (
    capture_num == 0 || capture_num >= parser->capture_nodes_by_num_len ||
    parser->capture_nodes_by_num[capture_num] == NULL || capture_num >= parser->capture_entry_index_by_num_len ||
    parser->capture_entry_index_by_num[capture_num] == SIZE_MAX
  ) {
    set_error_span_from_node(parser, node);
    return NK_ERR_UNDEFINED_SUBEXP_CALL;
  }
  node->call.resolved_capture_num = capture_num;
  return NK_SUCCESS;
}

static nk_error_t resolve_refs_impl(nk_parser_t* parser, nk_node_t* node, size_t* named_seen_counts) {
  if (node == NULL) {
    return NK_SUCCESS;
  }

  switch (node->base.type) {
    case NK_NODE_TYPE_UNKNOWN:
    case NK_NODE_TYPE_LITERAL:
    case NK_NODE_TYPE_CHAR_CLASS:
    case NK_NODE_TYPE_CHAR_TYPE:
    case NK_NODE_TYPE_CHAR_PROP:
    case NK_NODE_TYPE_DOT:
    case NK_NODE_TYPE_NEWLINE:
    case NK_NODE_TYPE_GRAPHEME_CLUSTER:
    case NK_NODE_TYPE_KEEP:
      return NK_SUCCESS;
    case NK_NODE_TYPE_BACK_REF:
      return resolve_back_ref(parser, node, named_seen_counts);
    case NK_NODE_TYPE_CALL:
      return resolve_call(parser, node);
    case NK_NODE_TYPE_ASSERTION:
      return resolve_refs_impl(parser, node->assertion.child, named_seen_counts);
    case NK_NODE_TYPE_QUANTIFIER:
      return resolve_refs_impl(parser, node->quantifier.child, named_seen_counts);
    case NK_NODE_TYPE_CAPTURE:
      if (
        parser->has_named_captures && node->capture.has_name && node->capture.capture_num > 0 &&
        node->capture.capture_num < parser->capture_entry_index_by_num_len
      ) {
        size_t entry_index = parser->capture_entry_index_by_num[node->capture.capture_num];
        if (entry_index == SIZE_MAX || entry_index >= parser->capture_name_map.entries_len) {
          return NK_ERR_PARSER_BUG;
        }
        named_seen_counts[entry_index] += 1;
      }
      return resolve_refs_impl(parser, node->capture.child, named_seen_counts);
    case NK_NODE_TYPE_GROUP:
      return resolve_refs_impl(parser, node->group.child, named_seen_counts);
    case NK_NODE_TYPE_ATOMIC:
      return resolve_refs_impl(parser, node->atomic.child, named_seen_counts);
    case NK_NODE_TYPE_ABSENCE:
      return resolve_refs_impl(parser, node->absence.child, named_seen_counts);
    case NK_NODE_TYPE_CONDITIONAL:
    {
      nk_error_t err = resolve_conditional(parser, node, named_seen_counts);
      if (err != NK_SUCCESS) {
        return err;
      }
      err = resolve_refs_impl(parser, node->conditional.yes_child, named_seen_counts);
      if (err != NK_SUCCESS) {
        return err;
      }
      return resolve_refs_impl(parser, node->conditional.no_child, named_seen_counts);
    }
    case NK_NODE_TYPE_CONCAT:
      for (size_t i = 0; i < node->concat.children_len; i++) {
        nk_error_t err = resolve_refs_impl(parser, node->concat.children[i], named_seen_counts);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      return NK_SUCCESS;
    case NK_NODE_TYPE_ALT:
      for (size_t i = 0; i < node->alt.children_len; i++) {
        nk_error_t err = resolve_refs_impl(parser, node->alt.children[i], named_seen_counts);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      return NK_SUCCESS;
  }

  return NK_ERR_PARSER_BUG;
}

static nk_error_t resolve_refs(nk_parser_t* parser, nk_node_t* root_node) {
  if (root_node == NULL) {
    return NK_SUCCESS;
  }

  size_t* named_seen_counts = NULL;
  if (parser->capture_name_map.entries_len > 0) {
    named_seen_counts = (size_t*)calloc(parser->capture_name_map.entries_len, sizeof(size_t));
    if (named_seen_counts == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
  }

  nk_error_t err = resolve_refs_impl(parser, root_node, named_seen_counts);
  free(named_seen_counts);
  return err;
}

nk_error_t nk_parser_postprocess(nk_parser_t* parser, nk_node_t* root_node) {
  parser->error_bytes = NULL;
  parser->error_bytes_end = NULL;
  clear_capture_maps(parser);
  clear_resolved_refs_impl(root_node);

  nk_error_t err = set_capture_num(parser, root_node);
  if (err != NK_SUCCESS) {
    return err;
  }

  err = build_capture_maps(parser, root_node);
  if (err != NK_SUCCESS) {
    return err;
  }

  return resolve_refs(parser, root_node);
}

nk_error_t
nk_node_get_resolved_capture_nums(const nk_parser_t* parser, const nk_node_t* node, uint32_t* out_capture_nums) {
  if (parser == NULL || node == NULL || out_capture_nums == NULL) {
    return NK_ERR_INTERNAL_ERROR;
  }

  size_t entry_index = NK_CAPTURE_NAME_MAP_ENTRY_INDEX_UNRESOLVED;
  size_t count = 0;
  switch (node->base.type) {
    case NK_NODE_TYPE_BACK_REF:
      entry_index = node->back_ref.resolved_capture_name_map_entry_index;
      count = node->back_ref.resolved_capture_num_count;
      break;
    case NK_NODE_TYPE_CONDITIONAL:
      entry_index = node->conditional.resolved_capture_name_map_entry_index;
      count = node->conditional.resolved_capture_num_count;
      break;
    default:
      return NK_ERR_INTERNAL_ERROR;
  }

  if (count == 0) {
    return NK_SUCCESS;
  }
  if (
    entry_index == NK_CAPTURE_NAME_MAP_ENTRY_INDEX_UNRESOLVED || entry_index >= parser->capture_name_map.entries_len
  ) {
    return NK_ERR_INTERNAL_ERROR;
  }

  const nk_capture_name_map_entry_t* entry = &parser->capture_name_map.entries[entry_index];
  if (count > entry->capture_nums_len) {
    return NK_ERR_INTERNAL_ERROR;
  }

  memcpy(out_capture_nums, entry->capture_nums, sizeof(uint32_t) * count);
  return NK_SUCCESS;
}
