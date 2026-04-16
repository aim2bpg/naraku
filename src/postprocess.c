#include <naraku_syntax.h>
#include <naraku_syntax_internal.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CAPTURE_NAMES_MAP_MIN_BUCKETS 16

static inline bool checked_add_size(size_t left, size_t right, size_t* out) {
  if (left > SIZE_MAX - right) {
    return false;
  }
  *out = left + right;
  return true;
}

static inline bool checked_mul_size(size_t left, size_t right, size_t* out) {
  if (left != 0 && right > SIZE_MAX / left) {
    return false;
  }
  *out = left * right;
  return true;
}

static inline bool can_mul_size(size_t left, size_t right) {
  return left == 0 || right <= SIZE_MAX / left;
}

static inline bool checked_realloc_mul(size_t count, size_t elem_size, size_t* out_size) {
  return checked_mul_size(count, elem_size, out_size);
}

static inline bool should_rehash_after_put(const nk_capture_names_map_t* map, bool* out_should_rehash) {
  size_t used_after_put = 0;
  if (!checked_add_size(map->buckets_used, 1, &used_after_put)) {
    return false;
  }

  // Rehash threshold is 70%: ceil(map->buckets_len * 7 / 10), computed without overflow.
  size_t q = map->buckets_len / 10;
  size_t r = map->buckets_len % 10;

  size_t base = 0;
  if (!checked_mul_size(q, 7, &base)) {
    return false;
  }
  size_t threshold = 0;
  if (!checked_add_size(base, (r * 7 + 9) / 10, &threshold)) {
    return false;
  }

  *out_should_rehash = used_after_put >= threshold;
  return true;
}

static bool pbuf_equal(const nk_pbuf_t* left, const nk_pbuf_t* right) {
  size_t left_len = (size_t)(left->bytes_end - left->bytes);
  size_t right_len = (size_t)(right->bytes_end - right->bytes);
  return left_len == right_len && memcmp(left->bytes, right->bytes, left_len) == 0;
}

static uint64_t pbuf_hash(const nk_pbuf_t* pbuf) {
  uint64_t h = 1469598103934665603ULL;
  for (const uint8_t* p = pbuf->bytes; p < pbuf->bytes_end; p++) {
    h ^= *p;
    h *= 1099511628211ULL;
  }
  return h;
}

static nk_error_t pbuf_to_owned_copy(const nk_pbuf_t* src, nk_pbuf_t* dst) {
  size_t len = (size_t)(src->bytes_end - src->bytes);
  uint8_t* bytes = (uint8_t*)malloc(len);
  if (bytes == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  memcpy(bytes, src->bytes, len);
  *dst = (nk_pbuf_t){.type = NK_PBUF_OWNED, .cap = len, .bytes = bytes, .bytes_end = bytes + len};
  return NK_SUCCESS;
}

nk_error_t capture_names_map_init(nk_capture_names_map_t** out_map) {
  nk_capture_names_map_t* map = (nk_capture_names_map_t*)malloc(sizeof(nk_capture_names_map_t));
  if (map == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  if (!can_mul_size(CAPTURE_NAMES_MAP_MIN_BUCKETS, sizeof(capture_names_map_bucket_t))) {
    free(map);
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  map->buckets = (capture_names_map_bucket_t*)calloc(CAPTURE_NAMES_MAP_MIN_BUCKETS, sizeof(capture_names_map_bucket_t));
  if (map->buckets == NULL) {
    free(map);
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  map->buckets_len = CAPTURE_NAMES_MAP_MIN_BUCKETS;
  map->buckets_used = 0;
  *out_map = map;
  return NK_SUCCESS;
}

void capture_names_map_free(nk_capture_names_map_t* map) {
  if (map == NULL) {
    return;
  }
  if (map->buckets != NULL) {
    for (size_t i = 0; i < map->buckets_len; i++) {
      if (map->buckets[i].in_use) {
        nk_pbuf_free(&map->buckets[i].name_buf);
      }
    }
    free(map->buckets);
  }
  free(map);
}

static nk_error_t capture_names_map_rehash(nk_capture_names_map_t* map, size_t new_len) {
  if (new_len == 0) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  capture_names_map_bucket_t* old_buckets = map->buckets;
  size_t old_len = map->buckets_len;
  if (!can_mul_size(new_len, sizeof(capture_names_map_bucket_t))) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  capture_names_map_bucket_t* new_buckets =
    (capture_names_map_bucket_t*)calloc(new_len, sizeof(capture_names_map_bucket_t));
  if (new_buckets == NULL) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }

  map->buckets = new_buckets;
  map->buckets_len = new_len;
  map->buckets_used = 0;

  for (size_t i = 0; i < old_len; i++) {
    capture_names_map_bucket_t* old = &old_buckets[i];
    if (!old->in_use) {
      continue;
    }

    uint64_t h = pbuf_hash(&old->name_buf);
    size_t index = (size_t)(h % map->buckets_len);
    while (map->buckets[index].in_use) {
      index = (index + 1) % map->buckets_len;
    }
    map->buckets[index] = *old;
    map->buckets[index].in_use = true;
    map->buckets_used++;
  }

  free(old_buckets);
  return NK_SUCCESS;
}

nk_error_t capture_names_map_get(const nk_capture_names_map_t* map, const nk_pbuf_t* name_buf, size_t* out_index) {
  if (map == NULL || map->buckets_len == 0) {
    return NK_ERR_UNDEFINED_BACK_REF;
  }

  uint64_t h = pbuf_hash(name_buf);
  size_t index = (size_t)(h % map->buckets_len);
  for (size_t i = 0; i < map->buckets_len; i++) {
    const capture_names_map_bucket_t* bucket = &map->buckets[index];
    if (!bucket->in_use) {
      return NK_ERR_UNDEFINED_BACK_REF;
    }

    if (pbuf_equal(&bucket->name_buf, name_buf)) {
      *out_index = bucket->capture_entry_index;
      return NK_SUCCESS;
    }
    index = (index + 1) % map->buckets_len;
  }

  return NK_ERR_UNDEFINED_BACK_REF;
}

nk_error_t capture_names_map_put(nk_capture_names_map_t* map, const nk_pbuf_t* name_buf, size_t capture_entry_index) {
  bool rehash = false;
  if (!should_rehash_after_put(map, &rehash)) {
    return NK_ERR_MEMORY_ALLOCATION_FAILED;
  }
  if (rehash) {
    size_t new_buckets_len = 0;
    if (!checked_mul_size(map->buckets_len, 2, &new_buckets_len)) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    nk_error_t err = capture_names_map_rehash(map, new_buckets_len);
    if (err != NK_SUCCESS) {
      return err;
    }
  }

  uint64_t h = pbuf_hash(name_buf);
  size_t index = (size_t)(h % map->buckets_len);
  for (size_t i = 0; i < map->buckets_len; i++) {
    capture_names_map_bucket_t* bucket = &map->buckets[index];
    if (!bucket->in_use) {
      nk_error_t err = pbuf_to_owned_copy(name_buf, &bucket->name_buf);
      if (err != NK_SUCCESS) {
        return err;
      }

      bucket->capture_entry_index = capture_entry_index;
      bucket->in_use = true;
      map->buckets_used++;
      return NK_SUCCESS;
    }

    if (pbuf_equal(&bucket->name_buf, name_buf)) {
      bucket->capture_entry_index = capture_entry_index;
      return NK_SUCCESS;
    }

    index = (index + 1) % map->buckets_len;
  }
  return NK_ERR_INTERNAL_ERROR;
}

static inline void set_error_span_from_node(nk_parser_t* parser, nk_node_t* node) {
  parser->error_bytes = parser->pattern_bytes_begin + node->base.span_offset;
  parser->error_bytes_end = parser->error_bytes + node->base.span_length;
}

static inline void report_warning_from_node(nk_parser_t* parser, nk_warning_t warning, nk_node_t* node) {
  if (parser->warning_func != NULL) {
    parser->warning_func(parser, warning, node->base.span_offset, node->base.span_length);
  }
}

static void clear_capture_data(nk_parser_t* parser) {
  if (parser->capture_entries != NULL) {
    for (size_t i = 0; i < parser->capture_entries_len; i++) {
      free(parser->capture_entries[i].capture_nums);
    }
    free(parser->capture_entries);
    parser->capture_entries = NULL;
  }
  parser->capture_entries_len = 0;
  parser->capture_entries_cap = 0;

  if (parser->capture_names_map != NULL) {
    capture_names_map_free(parser->capture_names_map);
    parser->capture_names_map = NULL;
  }
}

static nk_error_t append_capture_num(nk_capture_entry_t* entry, uint32_t capture_num) {
  if (entry->capture_nums_len >= entry->capture_nums_cap) {
    size_t new_cap = 0;
    if (entry->capture_nums_cap == 0) {
      new_cap = 4;
    } else {
      if (!checked_mul_size(entry->capture_nums_cap, 2, &new_cap)) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
    }
    size_t alloc_size = 0;
    if (!checked_realloc_mul(new_cap, sizeof(uint32_t), &alloc_size)) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    uint32_t* new_capture_nums = (uint32_t*)realloc(entry->capture_nums, alloc_size);
    if (new_capture_nums == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    entry->capture_nums = new_capture_nums;
    entry->capture_nums_cap = new_cap;
  }
  if (entry->capture_nums_len == SIZE_MAX) {
    return NK_ERR_TOO_MANY_CAPTURE_GROUPS;
  }
  entry->capture_nums[entry->capture_nums_len++] = capture_num;
  return NK_SUCCESS;
}

static nk_error_t append_capture_entry(nk_parser_t* parser, size_t* out_index) {
  if (parser->capture_entries_len >= parser->capture_entries_cap) {
    size_t new_cap = 0;
    if (parser->capture_entries_cap == 0) {
      new_cap = 8;
    } else {
      if (!checked_mul_size(parser->capture_entries_cap, 2, &new_cap)) {
        return NK_ERR_MEMORY_ALLOCATION_FAILED;
      }
    }
    size_t alloc_size = 0;
    if (!checked_realloc_mul(new_cap, sizeof(nk_capture_entry_t), &alloc_size)) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    nk_capture_entry_t* new_entries = (nk_capture_entry_t*)realloc(parser->capture_entries, alloc_size);
    if (new_entries == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    parser->capture_entries = new_entries;
    parser->capture_entries_cap = new_cap;
  }
  if (parser->capture_entries_len == SIZE_MAX) {
    return NK_ERR_TOO_MANY_CAPTURE_GROUPS;
  }
  *out_index = parser->capture_entries_len;
  parser->capture_entries[parser->capture_entries_len] = (nk_capture_entry_t){0};
  parser->capture_entries_len++;
  return NK_SUCCESS;
}

static nk_error_t get_or_add_capture_entry_for_name(nk_parser_t* parser, const nk_pbuf_t* name_buf, size_t* out_index) {
  if (parser->capture_names_map == NULL) {
    nk_error_t err = capture_names_map_init(&parser->capture_names_map);
    if (err != NK_SUCCESS) {
      return err;
    }
  }

  if (capture_names_map_get(parser->capture_names_map, name_buf, out_index) == NK_SUCCESS) {
    return NK_SUCCESS;
  }

  nk_error_t err = append_capture_entry(parser, out_index);
  if (err != NK_SUCCESS) {
    return err;
  }
  return capture_names_map_put(parser->capture_names_map, name_buf, *out_index);
}

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
    {
      if (node->capture.has_name) {
        node->capture.capture_num = *next_capture_num;
        if (*next_capture_num == UINT32_MAX) {
          return NK_ERR_TOO_MANY_CAPTURE_GROUPS;
        }
        *next_capture_num += 1;
        return set_capture_num_impl(node->capture.child, next_capture_num);
      }

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
  if (!parser->has_named_captures) {
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

static nk_error_t collect_named_capture_entries_impl(nk_parser_t* parser, nk_node_t* node) {
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
      return collect_named_capture_entries_impl(parser, node->assertion.child);
    case NK_NODE_TYPE_QUANTIFIER:
      return collect_named_capture_entries_impl(parser, node->quantifier.child);
    case NK_NODE_TYPE_CAPTURE:
    {
      size_t entry_index = SIZE_MAX;
      nk_error_t err = get_or_add_capture_entry_for_name(parser, &node->capture.name_buf, &entry_index);
      if (err != NK_SUCCESS) {
        return err;
      }
      err = append_capture_num(&parser->capture_entries[entry_index], node->capture.capture_num);
      if (err != NK_SUCCESS) {
        return err;
      }
      return collect_named_capture_entries_impl(parser, node->capture.child);
    }
    case NK_NODE_TYPE_GROUP:
      return collect_named_capture_entries_impl(parser, node->group.child);
    case NK_NODE_TYPE_ATOMIC:
      return collect_named_capture_entries_impl(parser, node->atomic.child);
    case NK_NODE_TYPE_ABSENCE:
      return collect_named_capture_entries_impl(parser, node->absence.child);
    case NK_NODE_TYPE_CONDITIONAL:
    {
      nk_error_t err = collect_named_capture_entries_impl(parser, node->conditional.yes_child);
      if (err != NK_SUCCESS) {
        return err;
      }
      return collect_named_capture_entries_impl(parser, node->conditional.no_child);
    }
    case NK_NODE_TYPE_CONCAT:
      for (size_t i = 0; i < node->concat.children_len; i++) {
        nk_error_t err = collect_named_capture_entries_impl(parser, node->concat.children[i]);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      return NK_SUCCESS;
    case NK_NODE_TYPE_ALT:
      for (size_t i = 0; i < node->alt.children_len; i++) {
        nk_error_t err = collect_named_capture_entries_impl(parser, node->alt.children[i]);
        if (err != NK_SUCCESS) {
          return err;
        }
      }
      return NK_SUCCESS;
  }
  return NK_ERR_PARSER_BUG;
}

static nk_error_t build_capture_entries(nk_parser_t* parser, nk_node_t* root_node) {
  if (root_node == NULL) {
    return NK_SUCCESS;
  }
  if (parser->has_named_captures) {
    return collect_named_capture_entries_impl(parser, root_node);
  }

  for (uint32_t capture_num = 1; capture_num <= parser->num_capture_groups; capture_num++) {
    size_t entry_index = SIZE_MAX;
    nk_error_t err = append_capture_entry(parser, &entry_index);
    if (err != NK_SUCCESS) {
      return err;
    }
    err = append_capture_num(&parser->capture_entries[entry_index], capture_num);
    if (err != NK_SUCCESS) {
      return err;
    }
  }
  return NK_SUCCESS;
}

static nk_error_t find_capture_entry_index(nk_parser_t* parser, const nk_pbuf_t* name_buf, size_t* out_index) {
  if (parser->capture_names_map == NULL) {
    return NK_ERR_UNDEFINED_BACK_REF;
  }
  return capture_names_map_get(parser->capture_names_map, name_buf, out_index);
}

static inline bool is_valid_numeric_capture_ref(const nk_parser_t* parser, uint32_t capture_num) {
  return capture_num > 0 && capture_num <= parser->num_capture_groups;
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
      node->back_ref.resolved_capture_name_map_entry_index = NK_CAPTURE_NAME_MAP_ENTRY_INDEX_UNRESOLVED;
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
      node->conditional.resolved_capture_name_map_entry_index = NK_CAPTURE_NAME_MAP_ENTRY_INDEX_UNRESOLVED;
      node->conditional.resolved_capture_num_count = 0;
      clear_resolved_refs_impl(node->conditional.yes_child);
      clear_resolved_refs_impl(node->conditional.no_child);
      return;
    case NK_NODE_TYPE_CONCAT:
      for (size_t i = 0; i < node->concat.children_len; i++) clear_resolved_refs_impl(node->concat.children[i]);
      return;
    case NK_NODE_TYPE_ALT:
      for (size_t i = 0; i < node->alt.children_len; i++) clear_resolved_refs_impl(node->alt.children[i]);
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
    if (find_capture_entry_index(parser, &node->back_ref.name_buf, &entry_index) != NK_SUCCESS) {
      set_error_span_from_node(parser, node);
      return NK_ERR_UNDEFINED_BACK_REF;
    }
    size_t resolved_count = named_seen_counts == NULL ? 0 : named_seen_counts[entry_index];
    if (resolved_count == 0) {
      set_error_span_from_node(parser, node);
      return NK_ERR_UNDEFINED_BACK_REF;
    }
    if (resolved_count < parser->capture_entries[entry_index].capture_nums_len) {
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
  if (!is_valid_numeric_capture_ref(parser, node->back_ref.capture_num)) {
    set_error_span_from_node(parser, node);
    return NK_ERR_UNDEFINED_BACK_REF;
  }
  node->back_ref.resolved_capture_name_map_entry_index = (size_t)node->back_ref.capture_num - 1;
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
    if (find_capture_entry_index(parser, &node->conditional.name_buf, &entry_index) != NK_SUCCESS) {
      set_error_span_from_node(parser, node);
      return NK_ERR_UNDEFINED_CONDITIONAL_REF;
    }
    size_t resolved_count = named_seen_counts == NULL ? 0 : named_seen_counts[entry_index];
    if (resolved_count == 0) {
      set_error_span_from_node(parser, node);
      return NK_ERR_UNDEFINED_CONDITIONAL_REF;
    }
    if (resolved_count < parser->capture_entries[entry_index].capture_nums_len) {
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
  if (!is_valid_numeric_capture_ref(parser, node->conditional.capture_num)) {
    set_error_span_from_node(parser, node);
    return NK_ERR_UNDEFINED_CONDITIONAL_REF;
  }
  node->conditional.resolved_capture_name_map_entry_index = (size_t)node->conditional.capture_num - 1;
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
    if (find_capture_entry_index(parser, &node->call.name_buf, &entry_index) != NK_SUCCESS) {
      set_error_span_from_node(parser, node);
      return NK_ERR_UNDEFINED_SUBEXP_CALL;
    }
    nk_capture_entry_t* entry = &parser->capture_entries[entry_index];
    if (entry->capture_nums_len != 1) {
      set_error_span_from_node(parser, node);
      return NK_ERR_INVALID_SUBEXP_CALL;
    }
    node->call.resolved_capture_num = entry->capture_nums[0];
    return NK_SUCCESS;
  }

  if (node->call.target_kind != NK_CALL_TARGET_KIND_CAPTURE_NUM) {
    set_error_span_from_node(parser, node);
    return NK_ERR_INVALID_SUBEXP_CALL;
  }
  if (!is_valid_numeric_capture_ref(parser, node->call.capture_num)) {
    set_error_span_from_node(parser, node);
    return NK_ERR_UNDEFINED_SUBEXP_CALL;
  }
  node->call.resolved_capture_num = node->call.capture_num;
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
      if (parser->has_named_captures && node->capture.has_name) {
        size_t entry_index = SIZE_MAX;
        if (find_capture_entry_index(parser, &node->capture.name_buf, &entry_index) != NK_SUCCESS) {
          return NK_ERR_PARSER_BUG;
        }
        if (named_seen_counts[entry_index] == SIZE_MAX) {
          return NK_ERR_TOO_MANY_CAPTURE_GROUPS;
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
  if (parser->capture_entries_len > 0 && parser->has_named_captures) {
    if (!can_mul_size(parser->capture_entries_len, sizeof(size_t))) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
    named_seen_counts = (size_t*)calloc(parser->capture_entries_len, sizeof(size_t));
    if (named_seen_counts == NULL) {
      return NK_ERR_MEMORY_ALLOCATION_FAILED;
    }
  }

  nk_error_t err = resolve_refs_impl(parser, root_node, named_seen_counts);
  if (named_seen_counts != NULL) {
    free(named_seen_counts);
  }

  return err;
}

nk_error_t nk_parser_postprocess(nk_parser_t* parser, nk_node_t* root_node) {
  parser->error_bytes = NULL;
  parser->error_bytes_end = NULL;
  clear_capture_data(parser);
  clear_resolved_refs_impl(root_node);

  nk_error_t err = set_capture_num(parser, root_node);
  if (err != NK_SUCCESS) {
    return err;
  }

  err = build_capture_entries(parser, root_node);
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
  if (entry_index == NK_CAPTURE_NAME_MAP_ENTRY_INDEX_UNRESOLVED || entry_index >= parser->capture_entries_len) {
    return NK_ERR_INTERNAL_ERROR;
  }

  const nk_capture_entry_t* entry = &parser->capture_entries[entry_index];
  if (count > entry->capture_nums_len) {
    return NK_ERR_INTERNAL_ERROR;
  }
  memcpy(out_capture_nums, entry->capture_nums, sizeof(uint32_t) * count);
  return NK_SUCCESS;
}
