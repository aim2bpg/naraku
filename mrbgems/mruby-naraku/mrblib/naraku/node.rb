# frozen_string_literal: true

module Naraku
  class Node
    # Returns a Hash representation of this node, recursively converting
    # child nodes as well.
    def to_h
      h = { type: type }
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
        h[:is_ignore_case] = is_ignore_case
        h[:fold_flags] = fold_flags
        h[:has_name] = has_name
        h[:name] = name
        h[:group_num] = group_num
        h[:depth] = depth
      when :call
        h[:has_name] = has_name
        h[:name] = name
        h[:group_num] = group_num
      when :assertion
        h[:assertion_type] = assertion_type
        c = child
        h[:child] = c&.to_h
      when :quantifier
        h[:min] = min
        h[:max] = max
        h[:quantifier_type] = quantifier_type
        h[:child] = child.to_h
      when :group
        h[:has_name] = has_name
        h[:name] = name
        h[:group_num] = group_num
        h[:child] = child.to_h
      when :atomic
        h[:child] = child.to_h
      when :conditional
        h[:has_name] = has_name
        h[:name] = name
        h[:group_num] = group_num
        h[:yes_child] = yes_child&.to_h
        h[:no_child] = no_child&.to_h
      when :concat, :alt
        h[:children] = children.map(&:to_h)
      end
      h
    end

    class CharClassUnion
      def to_h
        { items: items.map(&:to_h) }
      end
    end

    class CharClassItem
      def to_h
        h = { type: type }
        case type
        when :code
          h[:code] = code
        when :range
          h[:from_code] = from_code
          h[:to_code] = to_code
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
