# frozen_string_literal: true

module Naraku
  class Node
    # Returns a Hash representation of this node, recursively converting
    # child nodes as well.
    def to_h
      h = { type:, span_offset:, span_length: }
      case type
      when :literal
        h[:buf] = buf
        h[:is_ignore_case] = is_ignore_case
        h[:fold_flags] = fold_flags
      when :char_class
        h[:is_strict] = is_strict
        h[:is_ignore_case] = is_ignore_case
        h[:fold_flags] = fold_flags
        h[:is_positive] = is_positive
        h[:unions] = unions.map(&:to_h)
      when :char_type
        h[:is_ignore_case] = is_ignore_case
        h[:fold_flags] = fold_flags
        h[:is_positive] = is_positive
        h[:is_ascii_only] = is_ascii_only
        h[:char_type] = char_type
      when :char_prop
        h[:is_ignore_case] = is_ignore_case
        h[:fold_flags] = fold_flags
        h[:is_positive] = is_positive
        h[:cprop] = cprop
      when :dot
        h[:allows_newline] = allows_newline
      when :newline, :grapheme_cluster, :keep
        # no extra fields
      when :back_ref
        h[:target_kind] = target_kind
        h[:is_ignore_case] = is_ignore_case
        h[:fold_flags] = fold_flags
        h[:has_name] = has_name
        h[:name] = name
        h[:capture_num] = capture_num
        h[:resolved_capture_nums] = resolved_capture_nums
        h[:has_depth] = has_depth
        h[:depth] = depth
      when :call
        h[:target_kind] = target_kind
        h[:has_name] = has_name
        h[:name] = name
        h[:capture_num] = capture_num
        h[:resolved_capture_num] = resolved_capture_num
      when :assertion
        h[:assertion_type] = assertion_type
        h[:child] = child&.to_h
      when :quantifier
        h[:min] = min
        h[:has_max] = has_max
        h[:max] = max
        h[:quantifier_type] = quantifier_type
        h[:child] = child.to_h
      when :capture
        h[:has_name] = has_name
        h[:name] = name
        h[:capture_num] = capture_num
        h[:child] = child.to_h
      when :group, :atomic, :absence
        h[:child] = child.to_h
      when :conditional
        h[:target_kind] = target_kind
        h[:has_name] = has_name
        h[:name] = name
        h[:capture_num] = capture_num
        h[:resolved_capture_nums] = resolved_capture_nums
        h[:has_depth] = has_depth
        h[:depth] = depth
        h[:yes_child] = yes_child&.to_h
        h[:no_child] = no_child&.to_h
      when :concat, :alt
        h[:children] = children.map(&:to_h)
      end
      h
    end

    class CharClassUnion
      def to_h
        { span_offset:, span_length:, items: items.map(&:to_h) }
      end
    end

    class CharClassItem
      def to_h
        h = { type:, span_offset:, span_length: }
        case type
        when :code
          h[:code] = code
        when :range
          h[:begin_code] = begin_code
          h[:end_code] = end_code
        when :char_type
          h[:is_positive] = is_positive
          h[:is_ascii_only] = is_ascii_only
          h[:char_type] = char_type
        when :posix_char_class
          h[:is_positive] = is_positive
          h[:is_ascii_only] = is_ascii_only
          h[:posix_char_class] = posix_char_class
        when :char_prop
          h[:is_positive] = is_positive
          h[:cprop] = cprop
        when :nested_char_class
          h[:is_positive] = is_positive
          h[:unions] = unions.map(&:to_h)
        end
        h
      end
    end
  end
end
