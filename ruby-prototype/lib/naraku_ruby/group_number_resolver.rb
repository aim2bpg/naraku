# frozen_string_literal: true

module NarakuRuby
  module GroupNumberResolver
    module_function

    def rewrite!(node)
      return node unless node.is_a?(Hash)
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
    private_class_method :contains_named_group?

    def rewrite_group_nodes(node, &block)
      yield node if node[:type] == :group

      each_child_node(node) do |child|
        rewrite_group_nodes(child, &block)
      end
    end
    private_class_method :rewrite_group_nodes

    def each_child_node(node, &block)
      case node[:type]
      when :assertion, :quantifier, :group, :atomic, :absence
        child = node[:child]
        yield child if child
      when :conditional
        yield node[:yes_child] if node[:yes_child]
        yield node[:no_child] if node[:no_child]
      when :concat, :alt
        (node[:children] || []).each(&block)
      end
    end
    private_class_method :each_child_node
  end
end
