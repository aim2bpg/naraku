# frozen_string_literal: true

module NarakuRuby
  module Preprocessors
    class EmptyMatchAnalyzer < Preprocessor
      LOOKBEHIND_TYPES = %i[positive_lookbehind negative_lookbehind].freeze
      CHILD_CONTAINER_TYPES = %i[group atomic absence].freeze

      def call(context)
        node = context[:node]
        return context unless node.is_a?(Hash)

        @group_nodes = build_group_nodes(context, node)
        @group_state = {}
        @group_empty = {}

        resolve_group_empty(0)
        validate_call_recursion!

        context.merge(node:)
      end

      private

      def build_group_nodes(context, root)
        group_nodes = (context[:group_nodes_by_num] || {}).dup
        group_nodes[0] = root
        group_nodes
      end

      def resolve_group_empty(group_num)
        state = @group_state[group_num]
        return @group_empty[group_num] if state == :done
        return false if state == :visiting

        scope_node = scope_node_for(group_num)
        return false unless scope_node

        @group_state[group_num] = :visiting
        @group_empty[group_num] = evaluate_node(scope_node, direction: :left, frontier: true)
        @group_state[group_num] = :done
        @group_empty[group_num]
      end

      def scope_node_for(group_num)
        group_node = @group_nodes[group_num]
        return nil unless group_node

        group_num.zero? ? group_node : group_node[:child]
      end

      def evaluate_node(node, direction:, frontier:)
        return false unless node.is_a?(Hash)

        is_empty = case node[:type]
                   when :literal, :char_class, :char_type, :char_prop, :dot, :newline, :grapheme_cluster
                     false
                   when :keep
                     true
                   when :assertion
                     assertion_empty?(node, direction, frontier)
                   when :back_ref
                     back_ref_empty?(node)
                   when :conditional
                     conditional_empty?(node, direction, frontier)
                   when :call
                     evaluate_call(node)
                   when :quantifier
                     evaluate_quantifier(node, direction, frontier)
                   when :group, :atomic, :absence
                     evaluate_child(node, direction, frontier)
                   when :concat
                     evaluate_concat(node, direction, frontier)
                   when :alt
                     evaluate_alt(node, direction, frontier)
                   end
        node[:is_empty] = is_empty
        is_empty
      end

      def assertion_empty?(node, direction, frontier)
        child_direction = direction
        child_direction = :right if LOOKBEHIND_TYPES.include?(node[:assertion_type])
        evaluate_node(node[:child], direction: child_direction, frontier:) if node[:child]
        true
      end

      def back_ref_empty?(node)
        resolved_group_nums = node[:resolved_group_nums]
        return false unless resolved_group_nums.is_a?(Array) && !resolved_group_nums.empty?

        resolved_group_nums.any? { |group_num| resolve_group_empty(group_num) }
      end

      def conditional_empty?(node, direction, frontier)
        yes_empty = evaluate_node(node[:yes_child], direction:, frontier:) if node[:yes_child]
        no_empty = evaluate_node(node[:no_child], direction:, frontier:) if node[:no_child]
        !!yes_empty || (!node[:no_child] || !!no_empty)
      end

      def evaluate_call(node)
        resolved_group_num = node[:resolved_group_num]
        return false unless resolved_group_num.is_a?(Integer)

        resolve_group_empty(resolved_group_num)
      end

      def evaluate_quantifier(node, direction, frontier)
        child_is_empty = evaluate_node(node[:child], direction:, frontier:)
        node[:min].zero? || child_is_empty
      end

      def evaluate_child(node, direction, frontier)
        child = node[:child]
        return false unless child

        evaluate_node(child, direction:, frontier:)
      end

      def evaluate_concat(node, direction, frontier)
        children = node[:children] || []
        ordered = direction == :right ? children.reverse : children
        all_empty = true
        active_frontier = frontier

        ordered.each do |child|
          child_is_empty = evaluate_node(child, direction:, frontier: active_frontier)
          all_empty &&= child_is_empty
          active_frontier &&= child_is_empty
        end

        all_empty
      end

      def evaluate_alt(node, direction, frontier)
        children = node[:children] || []
        children.any? { |child| evaluate_node(child, direction:, frontier:) }
      end

      def validate_call_recursion!
        reachable_states = collect_reachable_states
        entry_edges = build_entry_edges(reachable_states)
        detect_cycle_in_entry_edges(entry_edges, reachable_states)
      end

      def collect_entry_calls(node, direction:, frontier:)
        return [] if !frontier || !node.is_a?(Hash)

        type = node[:type]
        return entry_call_from_call_node(node, direction) if type == :call
        return entry_calls_from_assertion(node, direction) if type == :assertion
        return entry_calls_from_quantifier(node, direction) if type == :quantifier
        return collect_entry_calls(node[:child], direction:, frontier: true) if CHILD_CONTAINER_TYPES.include?(type)
        return collect_entry_calls_in_concat(node[:children] || [], direction:) if type == :concat
        return collect_entry_calls_in_alt(node, direction) if type == :alt
        return collect_entry_calls_in_conditional(node, direction) if type == :conditional

        []
      end

      def entry_call_from_call_node(node, direction)
        group_num = node[:resolved_group_num]
        return [] unless group_num.is_a?(Integer)

        [{ group_num:, direction: }]
      end

      def entry_calls_from_assertion(node, direction)
        child_direction = LOOKBEHIND_TYPES.include?(node[:assertion_type]) ? :right : direction
        collect_entry_calls(node[:child], direction: child_direction, frontier: true)
      end

      def entry_calls_from_quantifier(node, direction)
        return [] if node[:min].zero?

        collect_entry_calls(node[:child], direction:, frontier: true)
      end

      def collect_entry_calls_in_concat(children, direction:)
        ordered = direction == :right ? children.reverse : children
        calls = []
        ordered.each do |child|
          calls.concat(collect_entry_calls(child, direction:, frontier: true))
          break unless child[:is_empty]
        end
        calls.uniq
      end

      def collect_entry_calls_in_alt(node, direction)
        (node[:children] || []).flat_map { |child| collect_entry_calls(child, direction:, frontier: true) }.uniq
      end

      def collect_entry_calls_in_conditional(node, direction)
        yes_calls = collect_entry_calls(node[:yes_child], direction:, frontier: true)
        no_calls = collect_entry_calls(node[:no_child], direction:, frontier: true)
        (yes_calls + no_calls).uniq
      end

      def collect_reachable_states
        start = [0, :left]
        visited = { start => true }
        queue = [start]

        until queue.empty?
          group_num, direction = queue.shift
          scope = scope_node_for(group_num)
          next unless scope

          collect_all_calls(scope, direction:).each do |call|
            state = [call[:group_num], call[:direction]]
            next if visited[state]

            visited[state] = true
            queue << state
          end
        end

        visited.keys
      end

      def collect_all_calls(node, direction:)
        return [] unless node.is_a?(Hash)

        case node[:type]
        when :call
          entry_call_from_call_node(node, direction)
        when :assertion
          entry_calls_from_assertion(node, direction)
        when :quantifier
          return [] if node[:min].zero?

          collect_all_calls(node[:child], direction:)
        when :group, :atomic, :absence
          collect_all_calls(node[:child], direction:)
        when :concat, :alt
          (node[:children] || []).flat_map { |child| collect_all_calls(child, direction:) }.uniq
        when :conditional
          yes_calls = collect_all_calls(node[:yes_child], direction:)
          no_calls = collect_all_calls(node[:no_child], direction:)
          (yes_calls + no_calls).uniq
        else
          []
        end
      end

      def build_entry_edges(reachable_states)
        edges = Hash.new { |h, k| h[k] = [] }
        reachable_states.each do |state|
          group_num, direction = state
          scope = scope_node_for(group_num)
          next unless scope

          collect_entry_calls(scope, direction:, frontier: true).each do |call|
            edges[state] << [call[:group_num], call[:direction]]
          end
          edges[state].uniq!
        end
        edges
      end

      def detect_cycle_in_entry_edges(edges, reachable_states)
        visiting = {}
        visited = {}

        reachable_states.each do |state|
          next if visited[state]

          visit_entry_state(state, edges, visiting, visited)
        end
      end

      def visit_entry_state(state, edges, visiting, visited)
        visiting[state] = true
        edges[state].each do |next_state|
          raise_recursion_error(next_state[1], next_state[0]) if visiting[next_state]
          next if visited[next_state]

          visit_entry_state(next_state, edges, visiting, visited)
        end
        visiting.delete(state)
        visited[state] = true
      end

      def raise_recursion_error(direction, group_num)
        if direction == :right
          raise PreprocessError.new(
            :right_recursive_subexp_call_in_lookbehind,
            "right-recursive sub-expression call in lookbehind: \\g<#{group_num}>"
          )
        end

        raise PreprocessError.new(
          :left_recursive_subexp_call,
          "left-recursive sub-expression call: \\g<#{group_num}>"
        )
      end
    end
  end
end
