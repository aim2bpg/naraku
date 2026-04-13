# frozen_string_literal: true

module NarakuRuby
  module Preprocessors
    class GroupRefResolver < Preprocessor
      def call(context)
        node = context[:node]
        group_nodes_by_num = context[:group_nodes_by_num] || {}
        group_nums_by_name = context[:group_nums_by_name] || {}
        warnings = []

        if group_nums_by_name.empty?
          resolve_without_named_groups!(node, group_nodes_by_num)
        else
          resolve_with_named_groups!(node, group_nums_by_name, warnings)
        end

        context.merge(node:, warnings:)
      end

      private

      def resolve_with_named_groups!(node, all_group_nums_by_name, warnings)
        seen_group_nums_by_name = Hash.new { |h, k| h[k] = [] }

        each_node(node) do |current|
          case current[:type]
          when :group
            next unless current[:has_name]

            seen_group_nums_by_name[current[:name]] << current[:group_num]
          when :back_ref, :conditional
            resolve_back_ref_like_in_named_mode!(current, seen_group_nums_by_name, all_group_nums_by_name, warnings)
          when :call
            resolve_call_in_named_mode!(current, all_group_nums_by_name)
          end
        end
      end

      def resolve_call_in_named_mode!(node, all_group_nums_by_name)
        if node[:has_name]
          resolve_named_call!(node, all_group_nums_by_name)
        else
          resolve_numeric_call!(node, all_group_nums_by_name)
        end
      end

      def resolve_back_ref_like_in_named_mode!(node, seen_group_nums_by_name, all_group_nums_by_name, warnings)
        if node[:has_name]
          resolve_named_back_ref_like!(node, seen_group_nums_by_name, all_group_nums_by_name, warnings)
        else
          raise PreprocessError.new(
            :numeric_back_ref_with_named_groups,
            'numbered back-reference is not allowed when named groups exist'
          )
        end
      end

      def resolve_named_back_ref_like!(node, seen_group_nums_by_name, all_group_nums_by_name, warnings)
        name = node[:name]
        all_group_nums = all_group_nums_by_name[name]
        raise PreprocessError.new(:undefined_named_back_ref, "undefined named back-reference: #{name}") if all_group_nums.empty?

        resolved_group_nums = seen_group_nums_by_name[name]
        if resolved_group_nums.empty?
          raise PreprocessError.new(
            :forward_named_back_ref,
            "named back-reference appears before definition: #{name}"
          )
        end

        node[:resolved_group_nums] = resolved_group_nums.dup
        return if resolved_group_nums.length == all_group_nums.length

        warnings << {
          type: :named_back_ref_excludes_later_groups,
          message: "named back-reference only resolves groups defined so far: #{name}",
          name:,
          resolved_group_nums: resolved_group_nums.dup,
          all_group_nums: all_group_nums.dup,
        }
      end

      def resolve_named_call!(node, all_group_nums_by_name)
        name = node[:name]
        group_nums = all_group_nums_by_name[name]
        raise PreprocessError.new(:undefined_named_subexp_call, "undefined named sub-expression call: #{name}") if group_nums.empty?

        if group_nums.length > 1
          raise PreprocessError.new(
            :ambiguous_named_subexp_call,
            "named sub-expression call must resolve to exactly one group: #{name}"
          )
        end

        node[:resolved_group_num] = group_nums.first
      end

      def resolve_numeric_call!(node, all_group_nums_by_name)
        group_num = node[:group_num]
        all_group_nums = all_group_nums_by_name.values.flatten
        max_group_num = all_group_nums.max || 0
        unless valid_call_group_num?(group_num, max_group_num)
          raise PreprocessError.new(
            :subexp_call_group_num_out_of_range,
            "sub-expression call group number is out of range: #{group_num}"
          )
        end

        node[:resolved_group_num] = group_num
      end

      def resolve_without_named_groups!(node, group_nodes_by_num)
        max_group_num = group_nodes_by_num.keys.max || 0

        each_node(node) do |current|
          case current[:type]
          when :back_ref, :conditional
            resolve_back_ref_like_without_named_groups!(current, max_group_num)
          when :call
            resolve_call_without_named_groups!(current, max_group_num)
          end
        end
      end

      def resolve_back_ref_like_without_named_groups!(node, max_group_num)
        if node[:has_name]
          raise PreprocessError.new(
            :named_back_ref_without_named_groups,
            "named back-reference is not allowed without named groups: #{node[:name]}"
          )
        end

        group_num = node[:group_num]
        unless group_num.is_a?(Integer) && group_num.positive? && group_num <= max_group_num
          raise PreprocessError.new(
            :back_ref_group_num_out_of_range,
            "back-reference group number is out of range: #{group_num}"
          )
        end

        node[:resolved_group_nums] = [group_num]
      end

      def resolve_call_without_named_groups!(node, max_group_num)
        if node[:has_name]
          raise PreprocessError.new(
            :named_subexp_call_without_named_groups,
            "named sub-expression call is not allowed without named groups: #{node[:name]}"
          )
        end

        group_num = node[:group_num]
        unless valid_call_group_num?(group_num, max_group_num)
          raise PreprocessError.new(
            :subexp_call_group_num_out_of_range,
            "sub-expression call group number is out of range: #{group_num}"
          )
        end

        node[:resolved_group_num] = group_num
      end

      def valid_call_group_num?(group_num, max_group_num)
        return true if group_num.zero?

        group_num.is_a?(Integer) && group_num.positive? && group_num <= max_group_num
      end
    end
  end
end
