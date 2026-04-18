module NarakuRuby
  module DFA
    State = Struct.new(
      :id,
      :op,
      :code,           # for `:code`
      :char_class,     # for `:char_class`
      :newline,        # for `:dot`
      :assertion_type, # for `:assertion`
      :cap_num,        # for `:cap_begin` and `:cap_end`
      :check_id,       # for `:check_visited`, `:mark_epsilon`, `:check_epsilon`, `:code`, `:char_class`, `:dot`, and `:match`
      :next,           # for any state except `:match`
      :split_next,     # for `:split`
      keyword_init: true
    )

    class State
      def to_s
        case op
        in :code
          format('%s %s (%d) -> %03d', 'code', State.show_code(code), check_id, self.next.id)
        in :dot
          format('%s %s (%d) -> %03d', 'dot', newline.to_s, check_id, self.next.id)
        in :char_class
          lines = []
          lines << format('%s (%d) -> %03d', 'char_class', check_id, self.next.id)
          char_class.ranges.each do |range|
            lines << format('  %s..%s', State.show_code(range.begin), State.show_code(range.end))
          end
          lines.join("\n")
        in :assertion
          format('%s %s -> %03d', 'assertion', assertion_type, self.next.id)
        in :cap_begin
          format('%s %d -> %03d', 'cap_begin', cap_num, self.next.id)
        in :cap_end
          format('%s %d -> %03d', 'cap_end', cap_num, self.next.id)
        in :keep
          format('%s -> %03d', 'keep', self.next.id)
        in :check_visited
          format('%s %d -> %03d', 'check_visited', check_id, self.next.id)
        in :mark_epsilon
          format('%s %d -> %03d', 'mark_epsilon', check_id, self.next.id)
        in :check_epsilon
          format('%s %d -> %03d', 'check_epsilon', check_id, self.next.id)
        in :split
          format('%s -> %03d, %03d', 'split', self.next.id, split_next.id)
        in :jump
          format('%s -> %03d', 'jump', self.next.id)
        in :match
          format('%s (%d)', 'match', check_id)
        else
          format('unknown op: %s', op)
        end
      end

      def self.show_code(code)
        code < 0x20 || code == 0x7F ? format('\'\\x%02X\'', code) : "'#{code.chr(::Encoding::UTF_8)}'"
      end
    end

    Hole = Struct.new(
      :state,
      :target, # `:next` or `:split_next`
      keyword_init: true
    )

    class CompileError < StandardError
      def initialize(message, offset:, length:)
        span = if offset
                 length.zero? ? " (at offset #{offset})" : " (at span #{offset}..#{offset + length})"
               else
                 ''
               end
        super("#{message}#{span}")

        @offset = offset
        @length = length
      end

      attr_reader :offset, :length
    end

    class Compiler
      def initialize(context)
        @context = context

        @states = []
        @next_check_id = 0
        @next_epsilon_check_id = 0
      end

      def compile
        node = @context[:node]
        initial_state = emit_cap_begin(0)
        node_state, last_holes = compile_node(node)
        cap_end_state = emit_cap_end(0)
        initial_state.next = node_state
        patch_all(last_holes, cap_end_state)

        match_state = emit_match
        cap_end_state.next = match_state

        parser_info = @context[:parser_info]
        Program.new(states: @states, initial_state:, num_capture_groups: parser_info[:num_capture_groups], num_check_ids: @next_check_id)
      end

      private

      # Compiles the given node and returns two values:
      # 1. the initial state for the node
      # 2. a list of holes for patching the last emitted states
      def compile_node(node)
        type = node[:type]

        case type
        in :literal
          compile_literal(node)
        in :char_class | :char_type | :char_prop
          compile_char_class_like(node)
        in :dot
          compile_dot(node)
        in :assertion
          compile_assertion(node)
        in :keep
          compile_keep(node)
        in :group
          compile_group(node)
        in :capture
          compile_capture(node)
        in :quantifier
          compile_quantifier(node)
        in :concat
          compile_concat(node)
        in :alt
          compile_alt(node)
        else
          compile_error("unsupported node type: #{type}", node)
        end
      end

      def compile_literal(node)
        return compile_literal_ignore_case(node) if node[:is_ignore_case]

        initial_state, last_state = *nil
        node[:buf].each_codepoint do |code|
          code_state = emit_code(code)
          initial_state ||= code_state
          last_state.next = code_state if last_state
          last_state = code_state
        end

        [initial_state, [Hole.new(state: last_state, target: :next)]]
      end

      def compile_literal_ignore_case(node)
        buf = node[:buf]
        folded_buf = NarakuRuby::Encoding.case_fold(buf.bytes, node[:fold_flags])
        start_index, unfold_nodes = NarakuRuby::Encoding.expand_case_unfold(folded_buf, node[:fold_flags])
        compile_unfold_nodes(start_index, unfold_nodes)
      end

      def compile_unfold_nodes(start_index, unfold_nodes)
        compiled_nodes = {}
        queue = [[start_index, nil]]
        initial_state = nil
        last_holes = []

        while queue.any?
          index, parent_hole = queue.shift
          if compiled_nodes.key?(index)
            patch(parent_hole, compiled_nodes[index]) if parent_hole
            next
          end

          unfold_node = unfold_nodes[index]
          case unfold_node[:type]
          in :match
            last_holes << parent_hole
          in :code
            code_state = emit_code(unfold_node[:code])
            initial_state ||= code_state
            compiled_nodes[index] = code_state
            patch(parent_hole, code_state) if parent_hole

            queue << [unfold_node[:next], Hole.new(state: code_state, target: :next)]
          in :alt
            split_state = emit_split
            initial_state ||= split_state
            compiled_nodes[index] = split_state
            patch(parent_hole, split_state) if parent_hole

            children = unfold_node[:children]
            children.each_with_index do |child_index, i|
              if i == children.length - 1
                queue << [child_index, Hole.new(state: split_state, target: :split_next)]
                next
              end

              queue << [child_index, Hole.new(state: split_state, target: :next)]
              next unless i < children.length - 2

              next_split_state = emit_split
              split_state.split_next = next_split_state
              split_state = next_split_state
            end
          end
        end

        [initial_state, last_holes]
      end

      def compile_char_class_like(node)
        builder = CharClassBuilder.new(node)
        char_class, expanded_strings = builder.build

        if expanded_strings.empty?
          char_class_state = emit_char_class(char_class)
          return [char_class_state, [Hole.new(state: char_class_state, target: :next)]]
        end

        compile_error('multi-characters case folding is not allowed in strict mode', node) if node[:is_strict]

        initial_state = split_state = emit_split
        char_class_state = emit_char_class(char_class)
        split_state.next = char_class_state

        last_holes = [Hole.new(state: char_class_state, target: :next)]
        expanded_strings.each_with_index do |buf, index|
          # TODO: `node[:fold_flags] & (~1)` is a workaround for removing `:full` flag from `fold_flags`.
          # We don't want to unfold `ss` into `ß` in result again.
          # We should provide a better way to remove `:full` flag from `fold_flags` for this case.
          string_state, string_holes = compile_literal_ignore_case(buf:, fold_flags: node[:fold_flags] & (~1))
          last_holes.concat(string_holes)

          if index == expanded_strings.length - 1
            split_state.split_next = string_state
          else
            next_split_state = emit_split
            split_state.split_next = next_split_state
            next_split_state.next = string_state
            split_state = next_split_state
          end
        end

        [initial_state, last_holes]
      end

      def compile_dot(node)
        dot_state = emit_dot(node[:allows_newline])
        [dot_state, [Hole.new(state: dot_state, target: :next)]]
      end

      def compile_assertion(node)
        # TODO: Split lookaround assertions into separate node types.
        if node[:assertion_type].end_with?('lookahead') || node[:assertion_type].end_with?('lookbehind')
          compile_error('lookaround assertions are not supported', node)
        end

        assertion_state = emit_assertion(node[:assertion_type])
        [assertion_state, [Hole.new(state: assertion_state, target: :next)]]
      end

      def compile_keep(_node)
        keep_state = emit_keep
        [keep_state, [Hole.new(state: keep_state, target: :next)]]
      end

      def compile_group(node)
        compile_node(node[:child])
      end

      def compile_capture(node)
        cap_begin_state = emit_cap_begin(node[:capture_num])
        child_initial_state, child_holes = compile_node(node[:child])
        cap_end_state = emit_cap_end(node[:capture_num])
        cap_begin_state.next = child_initial_state
        patch_all(child_holes, cap_end_state)
        [cap_begin_state, [Hole.new(state: cap_end_state, target: :next)]]
      end

      def compile_quantifier(node)
        compile_error('possessive quantifier is not supported', node) if node[:quantifier_type] == :possessive

        next_target, split_target = node[:quantifier_type] == :greedy ? %i[next split_next] : %i[split_next next]
        initial_state = nil
        last_holes = []

        initial_state, last_holes = compile_n_times(node[:child], node[:min]) if node[:min].positive?

        if node[:has_max]
          if node[:min] == node[:max]
            return compile_epsilon if node[:min].zero?

            return initial_state, last_holes
          end

          max_initial_state, max_last_holes = compile_at_most_n_times(node[:child], node[:max] - node[:min], next_target, split_target)
          initial_state ||= max_initial_state
          patch_all(last_holes, max_initial_state)
          last_holes = max_last_holes
          return initial_state, last_holes
        end

        repeat_initial_state, repeat_last_holes = compile_zero_or_more(node[:child], next_target, split_target)
        initial_state ||= repeat_initial_state
        patch_all(last_holes, repeat_initial_state)
        last_holes = repeat_last_holes

        [initial_state, last_holes]
      end

      def compile_epsilon
        jump_state = emit_jump
        [jump_state, [Hole.new(state: jump_state, target: :next)]]
      end

      def compile_n_times(child, n)
        initial_state, last_holes = compile_node(child)
        (n - 1).times do
          next_initial_state, next_last_holes = compile_node(child)
          patch_all(last_holes, next_initial_state)
          last_holes = next_last_holes
        end

        [initial_state, last_holes]
      end

      def compile_at_most_n_times(child, n, next_target, split_target)
        can_match_empty = match_epsilon?(child)
        epsilon_check_id = nil
        if can_match_empty
          epsilon_check_id = @next_epsilon_check_id
          @next_epsilon_check_id += 1
        end

        initial_state = split_state = nil
        last_holes = []
        child_holes = []
        n.times do
          split_state = emit_split
          initial_state ||= split_state

          last_holes << Hole.new(state: split_state, target: split_target)
          patch_all(child_holes, split_state)

          child_state, child_holes, exit_holes = compile_node_with_epsilon_check(child, epsilon_check_id)
          split_state[next_target] = child_state
          last_holes.concat(exit_holes)
        end

        [initial_state, last_holes + child_holes]
      end

      def compile_zero_or_more(child, next_target, split_target)
        can_match_empty = match_epsilon?(child)
        epsilon_check_id = nil
        if can_match_empty
          epsilon_check_id = @next_epsilon_check_id
          @next_epsilon_check_id += 1
        end

        split_state = emit_split
        child_state, child_holes, exit_holes = compile_node_with_epsilon_check(child, epsilon_check_id)

        split_state[next_target] = child_state
        patch_all(child_holes, split_state)

        [split_state, [Hole.new(state: split_state, target: split_target)] + exit_holes]
      end

      def match_epsilon?(node)
        case node[:type]
        in :assertion | :keep
          true
        in :group | :capture
          match_epsilon?(node[:child])
        in :concat
          node[:children].all? { |child| match_epsilon?(child) }
        in :alt
          node[:children].any? { |child| match_epsilon?(child) }
        in :quantifier
          node[:min].zero? || match_epsilon?(node[:child])
        else
          false
        end
      end

      def compile_node_with_epsilon_check(node, epsilon_check_id)
        return *compile_node(node), [] unless epsilon_check_id

        mark_epsilon_state = emit_mark_epsilon(epsilon_check_id)
        node_state, node_holes = compile_node(node)
        mark_epsilon_state.next = node_state

        check_epsilon_state = emit_check_epsilon(epsilon_check_id)
        patch_all(node_holes, check_epsilon_state)

        [mark_epsilon_state, [Hole.new(state: check_epsilon_state, target: :next)], [Hole.new(state: check_epsilon_state, target: :split_next)]]
      end

      def compile_concat(node)
        return compile_epsilon if node[:children].empty?

        initial_state = nil
        last_holes = []

        node[:children].each do |child|
          child_state, child_holes = compile_node(child)
          initial_state ||= child_state
          patch_all(last_holes, child_state)
          last_holes = child_holes
        end

        [initial_state, last_holes]
      end

      def compile_alt(node)
        initial_state = split_state = emit_split
        last_holes = []
        children = node[:children]
        children.each_with_index do |child, index|
          child_state, child_holes = compile_node(child)
          last_holes.concat(child_holes)

          if index == children.length - 1
            split_state.split_next = child_state
            next
          end

          split_state.next = child_state
          next unless index < children.length - 2

          next_split_state = emit_split
          split_state.split_next = next_split_state
          split_state = next_split_state
        end

        [initial_state, last_holes]
      end

      def patch_all(holes, target_state)
        return if holes.empty?

        if holes.length == 1
          patch(holes.first, target_state)
          return
        end

        check_id = @next_check_id
        @next_check_id += 1
        check_visited_state = emit_check_visited(check_id)
        check_visited_state.next = target_state

        holes.each { patch(it, check_visited_state) }
      end

      def patch(hole, target_state)
        hole.state[hole.target] = target_state
      end

      def emit_jump
        emit(:jump)
      end

      def emit_split
        emit(:split)
      end

      def emit_code(code)
        check_id = @next_check_id
        @next_check_id += 1
        emit(:code, code:, check_id:)
      end

      def emit_char_class(char_class)
        check_id = @next_check_id
        @next_check_id += 1
        emit(:char_class, char_class:, check_id:)
      end

      def emit_dot(newline)
        check_id = @next_check_id
        @next_check_id += 1
        emit(:dot, newline:, check_id:)
      end

      def emit_assertion(assertion_type)
        emit(:assertion, assertion_type:)
      end

      def emit_keep
        emit(:keep)
      end

      def emit_cap_begin(cap_num)
        emit(:cap_begin, cap_num:)
      end

      def emit_cap_end(cap_num)
        emit(:cap_end, cap_num:)
      end

      def emit_check_visited(check_id)
        emit(:check_visited, check_id:)
      end

      def emit_mark_epsilon(check_id)
        emit(:mark_epsilon, check_id:)
      end

      def emit_check_epsilon(check_id)
        emit(:check_epsilon, check_id:)
      end

      def emit_match
        check_id = @next_check_id
        @next_check_id += 1
        emit(:match, check_id:)
      end

      def emit(op, code: nil, char_class: nil, newline: nil, cap_num: nil, assertion_type: nil, check_id: nil, next: nil, split_next: nil)
        id = @states.length
        state = State.new(id:, op:, code:, char_class:, newline:, cap_num:, assertion_type:, check_id:, next:, split_next:)
        @states << state
        state
      end

      def compile_error(message, node)
        offset = node[:span_offset]
        length = node[:span_length]
        raise CompileError.new(message, offset:, length:)
      end
    end
  end
end
