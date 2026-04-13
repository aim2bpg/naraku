# frozen_string_literal: true

module NarakuRuby
  module Preprocessors
    class GroupNumberResolver < Preprocessor
      def call(context)
        node = context[:node]
        return context unless node.is_a?(Hash)

        rewrite_named_and_unnamed_groups!(node) if contains_named_group?(node)
        group_nodes_by_num, group_nums_by_name = collect_group_maps(node)

        context.merge(
          node:,
          group_nodes_by_num:,
          group_nums_by_name:
        )
      end

      private

      def rewrite_named_and_unnamed_groups!(node)
        return node unless contains_named_group?(node)

        next_group_num = 1
        rewrite_group_nodes(node) do |group_node|
          if group_node[:has_name]
            group_node[:group_num] = next_group_num
            next_group_num += 1
          else
            group_node[:group_num] = 0
          end
        end

        node
      end

      def contains_named_group?(node)
        return true if node[:type] == :group && node[:has_name]

        found = false
        each_child_node(node) do |child|
          found = true if contains_named_group?(child)
          break if found
        end
        found
      end

      def rewrite_group_nodes(node, &)
        yield node if node[:type] == :group

        each_child_node(node) do |child|
          rewrite_group_nodes(child, &)
        end
      end

      def collect_group_maps(node)
        group_nodes_by_num = {}
        group_nums_by_name = Hash.new { |h, k| h[k] = [] }

        each_node(node) do |current|
          next unless current[:type] == :group

          group_num = current[:group_num]
          next unless group_num.is_a?(Integer) && group_num.positive?

          group_nodes_by_num[group_num] = current
          group_nums_by_name[current[:name]] << group_num if current[:has_name]
        end

        [group_nodes_by_num, group_nums_by_name]
      end
    end
  end
end
