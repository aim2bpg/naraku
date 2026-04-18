module NarakuRuby
  module DFA
    class MatchData
      def initialize(string, caps)
        @string = string
        @caps = caps
      end

      def length
        @caps.length / 2
      end

      alias size length

      def [](index)
        cap = begin_end(index)
        return nil unless cap

        @string[cap[0]...cap[1]]
      end

      def begin(index = 0)
        begin_end(index)&.first
      end

      def end(index = 0)
        begin_end(index)&.last
      end

      def captures
        (1...length).map { |i| self[i] }
      end

      private

      def begin_end(index)
        return nil if index >= length

        cap_begin = @caps[index * 2]
        cap_end = @caps[(index * 2) + 1]
        return nil if cap_begin.negative? || cap_end.negative?

        [cap_begin, cap_end]
      end
    end

    class Program
      Thread = Struct.new(:state, :keep_pos, :caps)

      class Thread
        @pool = []

        def self.alloc(state, keep_pos, caps)
          if @pool.empty?
            new(state, keep_pos, caps)
          else
            instance = @pool.pop
            instance.state = state
            instance.keep_pos = keep_pos
            instance.caps = caps
            instance
          end
        end

        def self.release(instance)
          @pool << instance
        end
      end

      class Caps
        def initialize(data, shared: false)
          @data = data
          @shared = shared
        end

        def [](index)
          @data[index]
        end

        def fork
          @shared = true
          self.class.new(@data, shared: true)
        end

        def set_cap_begin(cap_num, pos)
          ensure_writable!
          begin_index = cap_num * 2
          @data[begin_index] = pos
          @data[begin_index + 1] = -1
        end

        def set_cap_end(cap_num, pos)
          ensure_writable!
          @data[(cap_num * 2) + 1] = pos
        end

        def materialize(keep_pos)
          ensure_writable!
          @data[0] = keep_pos
          @data
        end

        private

        def ensure_writable!
          return unless @shared

          @data = @data.dup
          @shared = false
        end
      end

      DEBUG = false

      def initialize(states:, initial_state:, num_capture_groups:, num_check_ids:)
        @states = states
        @initial_state = initial_state
        @num_capture_groups = num_capture_groups
        @num_check_ids = num_check_ids
        @caps_size = (@num_capture_groups + 1) * 2
        @empty_caps = Array.new(@caps_size, -1).freeze

        @word_char_class = @ascii_word_char_class = nil
        @word_ascii_table = @ascii_word_ascii_table = nil
      end

      attr_reader :states, :initial_state

      def show_states
        @states.each do |state|
          puts format('%03d: %s', state.id, state.to_s)
        end
        nil
      end

      def match(string, pos = 0)
        caps = run_with_caps(string, pos)
        caps ? MatchData.new(string, caps) : nil
      end

      def match?(string, pos = 0)
        run_without_caps(string, pos)
      end

      private

      def run_with_caps(string, start_pos)
        code_points = string.ascii_only? ? string.bytes : string.codepoints

        show_states if DEBUG

        threads = []
        next_threads = []
        visited_marks = Array.new(@num_check_ids, 0)
        visit_token = 1
        epsilon_closure_with_caps(
          code_points, start_pos, start_pos, @initial_state, start_pos,
          Caps.new(@empty_caps.dup), 0, visited_marks, visit_token, threads
        )

        best_keep_pos = nil
        best_caps = nil

        pos = start_pos
        while pos < code_points.length
          if DEBUG
            puts "pos: #{pos}"
            threads.each do |thread|
              p [thread.state.id, thread.state.op, thread.keep_pos, thread.caps]
            end
          end

          break if threads.any? && threads.first.state.op == :match

          code = code_points[pos]
          visit_token += 1
          has_match_thread = false
          threads.each do |thread|
            state = thread.state
            if has_match_thread
              Thread.release(thread)
              next
            end

            case state.op
            when :match
              if visited_marks[state.check_id] == visit_token
                Thread.release(thread)
                next
              end

              next_threads << thread
              visited_marks[state.check_id] = visit_token
              has_match_thread = true
              next
            when :code
              unless state.code == code
                Thread.release(thread)
                next
              end
            when :char_class
              unless state.char_class.fast_include?(code)
                Thread.release(thread)
                next
              end
            when :dot
              unless code != 0x0A || state.newline
                Thread.release(thread)
                next
              end
            else
              raise "unexpected state: #{state}"
            end

            Thread.release(thread)
            epsilon_closure_with_caps(code_points, start_pos, pos + 1, state.next, thread.keep_pos, thread.caps, 0, visited_marks, visit_token, next_threads)
          end

          unless has_match_thread
            epsilon_closure_with_caps(
              code_points, start_pos, pos + 1, @initial_state, pos + 1,
              Caps.new(@empty_caps.dup), 0, visited_marks, visit_token, next_threads
            )
          end

          threads, next_threads = next_threads, threads
          next_threads.clear
          pos += 1
        end

        if DEBUG
          puts "pos: #{pos}"
          threads.each do |thread|
            p [thread.state.id, thread.state.op, thread.keep_pos, thread.caps]
          end
        end

        threads.each do |thread|
          next unless thread.state.op == :match

          best_keep_pos = thread.keep_pos
          best_caps = thread.caps
          break
        end

        best_caps&.materialize(best_keep_pos)
      end

      def run_without_caps(string, start_pos)
        code_points = string.ascii_only? ? string.bytes : string.codepoints

        show_states if DEBUG

        threads = []
        next_threads = []
        visited_marks = Array.new(@num_check_ids, 0)
        visit_token = 1
        return true if epsilon_closure_without_caps(code_points, start_pos, start_pos, @initial_state, 0, visited_marks, visit_token, threads)

        pos = start_pos
        while pos < code_points.length
          code = code_points[pos]
          visit_token += 1
          threads.each do |state|
            case state.op
            when :match
              next if visited_marks[state.check_id] == visit_token

              return true
            when :code
              next unless state.code == code
            when :char_class
              next unless state.char_class.fast_include?(code)
            when :dot
              next unless code != 0x0A || state.newline
            else
              raise "unexpected state: #{state}"
            end

            return true if epsilon_closure_without_caps(
              code_points, start_pos, pos + 1, state.next, 0, visited_marks, visit_token, next_threads
            )
          end

          return true if epsilon_closure_without_caps(
            code_points, start_pos, pos + 1, @initial_state, 0,
            visited_marks, visit_token, next_threads
          )

          threads, next_threads = next_threads, threads
          next_threads.clear
          pos += 1
        end

        threads.any? { |state| state.op == :match }
      end

      def epsilon_closure_with_caps(code_points, start_pos, pos, state, keep_pos, caps, epsilon_bits, visited_marks, visit_token, next_threads)
        case state.op
        when :match, :code, :char_class, :dot
          return if visited_marks[state.check_id] == visit_token

          visited_marks[state.check_id] = visit_token
          next_threads << Thread.alloc(state, keep_pos, caps)
        when :jump
          epsilon_closure_with_caps(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited_marks, visit_token, next_threads)
        when :split
          epsilon_closure_with_caps(code_points, start_pos, pos, state.next, keep_pos, caps.fork, epsilon_bits, visited_marks, visit_token, next_threads)
          epsilon_closure_with_caps(code_points, start_pos, pos, state.split_next, keep_pos, caps, epsilon_bits, visited_marks, visit_token, next_threads)
        when :cap_begin
          caps.set_cap_begin(state.cap_num, pos)
          epsilon_closure_with_caps(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited_marks, visit_token, next_threads)
        when :cap_end
          caps.set_cap_end(state.cap_num, pos)
          epsilon_closure_with_caps(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited_marks, visit_token, next_threads)
        when :keep
          epsilon_closure_with_caps(code_points, start_pos, pos, state.next, pos, caps, epsilon_bits, visited_marks, visit_token, next_threads)
        when :assertion
          return unless match_assertion?(state.assertion_type, code_points, start_pos, pos)

          epsilon_closure_with_caps(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited_marks, visit_token, next_threads)
        when :check_visited
          return if visited_marks[state.check_id] == visit_token

          epsilon_closure_with_caps(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited_marks, visit_token, next_threads)
          visited_marks[state.check_id] = visit_token
        when :mark_epsilon
          epsilon_bits |= 1 << state.check_id
          epsilon_closure_with_caps(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited_marks, visit_token, next_threads)
        when :check_epsilon
          if epsilon_bits.nobits?(1 << state.check_id)
            epsilon_closure_with_caps(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited_marks, visit_token, next_threads)
          else
            epsilon_closure_with_caps(code_points, start_pos, pos, state.split_next, keep_pos, caps, epsilon_bits, visited_marks, visit_token, next_threads)
          end
        else
          raise "unexpected state: #{state}"
        end
      end

      def epsilon_closure_without_caps(code_points, start_pos, pos, state, epsilon_bits, visited_marks, visit_token, next_threads)
        case state.op
        when :match, :code, :char_class, :dot
          return false if visited_marks[state.check_id] == visit_token

          visited_marks[state.check_id] = visit_token
          next_threads << state
          state.op == :match
        when :jump, :cap_begin, :cap_end, :keep
          epsilon_closure_without_caps(code_points, start_pos, pos, state.next, epsilon_bits, visited_marks, visit_token, next_threads)
        when :split
          epsilon_closure_without_caps(code_points, start_pos, pos, state.next, epsilon_bits, visited_marks, visit_token, next_threads) ||
            epsilon_closure_without_caps(code_points, start_pos, pos, state.split_next, epsilon_bits, visited_marks, visit_token, next_threads)
        when :assertion
          return false unless match_assertion?(state.assertion_type, code_points, start_pos, pos)

          epsilon_closure_without_caps(code_points, start_pos, pos, state.next, epsilon_bits, visited_marks, visit_token, next_threads)
        when :check_visited
          return false if visited_marks[state.check_id] == visit_token

          matched = epsilon_closure_without_caps(code_points, start_pos, pos, state.next, epsilon_bits, visited_marks, visit_token, next_threads)
          visited_marks[state.check_id] = visit_token
          matched
        when :mark_epsilon
          epsilon_bits |= 1 << state.check_id
          epsilon_closure_without_caps(code_points, start_pos, pos, state.next, epsilon_bits, visited_marks, visit_token, next_threads)
        when :check_epsilon
          if epsilon_bits.nobits?(1 << state.check_id)
            epsilon_closure_without_caps(code_points, start_pos, pos, state.next, epsilon_bits, visited_marks, visit_token, next_threads)
          else
            epsilon_closure_without_caps(code_points, start_pos, pos, state.split_next, epsilon_bits, visited_marks, visit_token, next_threads)
          end
        else
          raise "unexpected state: #{state}"
        end
      end

      def match_assertion?(assertion_type, code_points, start_pos, pos)
        case assertion_type
        when :begin_of_line
          pos.zero? || (code_points[pos - 1] == 0x0A && pos != code_points.length)
        when :end_of_line
          pos == code_points.length || code_points[pos] == 0x0A
        when :begin_of_string
          pos.zero?
        when :end_of_string_strict
          pos == code_points.length
        when :end_of_string_loose
          pos == code_points.length || (pos == code_points.length - 1 && code_points[pos] == 0x0A)
        when :begin_of_matching
          pos == start_pos
        when :word_boundary
          prev_is_word = word_code?(code_points, pos - 1)
          curr_is_word = word_code?(code_points, pos)
          prev_is_word != curr_is_word
        when :non_word_boundary
          prev_is_word = word_code?(code_points, pos - 1)
          curr_is_word = word_code?(code_points, pos)
          prev_is_word == curr_is_word
        when :ascii_word_boundary
          prev_is_word = ascii_word_code?(code_points, pos - 1)
          curr_is_word = ascii_word_code?(code_points, pos)
          prev_is_word != curr_is_word
        when :non_ascii_word_boundary
          prev_is_word = ascii_word_code?(code_points, pos - 1)
          curr_is_word = ascii_word_code?(code_points, pos)
          prev_is_word == curr_is_word
        end
      end

      def word_code?(code_points, pos)
        return false if pos.negative? || pos >= code_points.length

        code = code_points[pos]
        return word_ascii_table[code] if code <= 0x7F

        word_char_class.fast_include?(code)
      end

      def word_char_class
        return @word_char_class if @word_char_class

        builder = CharClassBuilder.new({
          type: :char_type,
          char_type: :word,
          span_offset: 0,
          span_length: 0,
          is_positive: true,
          is_ignore_case: false,
          is_ascii_only: false,
          fold_flags: 0,
        })
        @word_char_class, = builder.build
        @word_char_class
      end

      def word_ascii_table
        return @word_ascii_table if @word_ascii_table

        @word_ascii_table = build_ascii_table(word_char_class)
      end

      def ascii_word_code?(code_points, pos)
        return false if pos.negative? || pos >= code_points.length

        code = code_points[pos]
        code <= 0x7F && ascii_word_ascii_table[code]
      end

      def ascii_word_char_class
        return @ascii_word_char_class if @ascii_word_char_class

        builder = CharClassBuilder.new({
          type: :char_type,
          span_offset: 0,
          span_length: 0,
          char_type: :word,
          is_positive: true,
          is_ignore_case: false,
          is_ascii_only: true,
          fold_flags: 0,
        })
        @ascii_word_char_class, = builder.build
        @ascii_word_char_class
      end

      def ascii_word_ascii_table
        return @ascii_word_ascii_table if @ascii_word_ascii_table

        @ascii_word_ascii_table = build_ascii_table(ascii_word_char_class)
      end

      def build_ascii_table(char_class)
        Array.new(0x80) { |code| char_class.include?(code) }.freeze
      end
    end
  end
end
