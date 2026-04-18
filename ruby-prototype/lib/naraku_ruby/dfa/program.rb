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

      class BitSet
        def initialize
          @bits = 0
        end

        def include?(n)
          @bits.allbits?(1 << n)
        end

        def add(n)
          @bits |= 1 << n
        end

        def clear
          @bits = 0
        end
      end

      DEBUG = false

      def initialize(states:, initial_state:, num_capture_groups:, num_check_ids:)
        @states = states
        @initial_state = initial_state
        @num_capture_groups = num_capture_groups
        @num_check_ids = num_check_ids

        @word_char_class = @ascii_word_char_class = nil
      end

      attr_reader :states, :initial_state

      def show_states
        @states.each do |state|
          puts format('%03d: %s', state.id, state.to_s)
        end
        nil
      end

      def match(string, pos = 0)
        caps = run(string, pos)
        caps ? MatchData.new(string, caps) : nil
      end

      def match?(string, pos = 0)
        !!match(string, pos)
      end

      private

      def run(string, start_pos)
        code_points = string.codepoints

        show_states if DEBUG

        threads = []
        visited = BitSet.new
        epsilon_closure(code_points, start_pos, start_pos, @initial_state, start_pos, [-1] * ((@num_capture_groups + 1) * 2), 0, visited, threads)

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

          break if threads.any? && threads.first.state.op == :match && better_caps?(threads.first.caps, best_caps)

          next_threads = []
          visited.clear
          threads.each do |thread|
            case thread.state.op
            in :match
              next if visited.include?(thread.state.check_id)

              next_threads << thread
              visited.add(thread.state.check_id)
              next
            in :code
              next unless thread.state.code == code_points[pos]
            in :char_class
              next unless thread.state.char_class.include?(code_points[pos])
            in :dot
              next unless code_points[pos] != 0x0A || thread.state.newline
            else
              raise "unexpected state: #{state}"
            end

            epsilon_closure(code_points, start_pos, pos + 1, thread.state.next, thread.keep_pos, thread.caps, 0, visited, next_threads)
          end

          epsilon_closure(code_points, start_pos, pos + 1, @initial_state, pos + 1, [-1] * ((@num_capture_groups + 1) * 2), 0, visited, next_threads)

          threads = next_threads
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

        best_caps[0] = best_keep_pos if best_caps
        best_caps
      end

      def epsilon_closure(code_points, start_pos, pos, state, keep_pos, caps, epsilon_bits, visited, next_threads)
        case state.op
        in :match | :code | :char_class | :dot
          return if visited.include?(state.check_id)

          visited.add(state.check_id)
          next_threads << Thread.new(state, keep_pos, caps)
        in :jump
          epsilon_closure(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited, next_threads)
        in :split
          epsilon_closure(code_points, start_pos, pos, state.next, keep_pos, caps.dup, epsilon_bits, visited, next_threads)
          epsilon_closure(code_points, start_pos, pos, state.split_next, keep_pos, caps, epsilon_bits, visited, next_threads)
        in :cap_begin
          caps[state.cap_num * 2] = pos
          caps[(state.cap_num * 2) + 1] = -1
          epsilon_closure(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited, next_threads)
        in :cap_end
          caps[(state.cap_num * 2) + 1] = pos
          epsilon_closure(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited, next_threads)
        in :keep
          epsilon_closure(code_points, start_pos, pos, state.next, pos, caps, epsilon_bits, visited, next_threads)
        in :assertion
          return unless match_assertion?(state.assertion_type, code_points, start_pos, pos)

          epsilon_closure(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited, next_threads)
        in :check_visited
          return if visited.include?(state.check_id)

          epsilon_closure(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited, next_threads)
          visited.add(state.check_id)
        in :mark_epsilon
          epsilon_bits |= 1 << state.check_id
          epsilon_closure(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited, next_threads)
        in :check_epsilon
          if epsilon_bits.nobits?(1 << state.check_id)
            epsilon_closure(code_points, start_pos, pos, state.next, keep_pos, caps, epsilon_bits, visited, next_threads)
          else
            epsilon_closure(code_points, start_pos, pos, state.split_next, keep_pos, caps, epsilon_bits, visited, next_threads)
          end
        else
          raise "unexpected state: #{state}"
        end
      end

      def better_caps?(caps, than_caps)
        than_caps.nil? || caps[0] < than_caps[0] || (caps[0] == than_caps[0] && caps[1] > than_caps[1])
      end

      def match_assertion?(assertion_type, code_points, start_pos, pos)
        case assertion_type
        in :begin_of_line
          pos.zero? || (code_points[pos - 1] == 0x0A && pos != code_points.length)
        in :end_of_line
          pos == code_points.length || code_points[pos] == 0x0A
        in :begin_of_string
          pos.zero?
        in :end_of_string_strict
          pos == code_points.length
        in :end_of_string_loose
          pos == code_points.length || (pos == code_points.length - 1 && code_points[pos] == 0x0A)
        in :begin_of_matching
          pos == start_pos
        in :word_boundary
          prev_is_word = word_code?(code_points, pos - 1)
          curr_is_word = word_code?(code_points, pos)
          prev_is_word != curr_is_word
        in :non_word_boundary
          prev_is_word = word_code?(code_points, pos - 1)
          curr_is_word = word_code?(code_points, pos)
          prev_is_word == curr_is_word
        in :ascii_word_boundary
          prev_is_word = ascii_word_code?(code_points, pos - 1)
          curr_is_word = ascii_word_code?(code_points, pos)
          prev_is_word != curr_is_word
        in :non_ascii_word_boundary
          prev_is_word = ascii_word_code?(code_points, pos - 1)
          curr_is_word = ascii_word_code?(code_points, pos)
          prev_is_word == curr_is_word
        end
      end

      def word_code?(code_points, pos)
        return false if pos.negative? || pos >= code_points.length

        word_char_class.include?(code_points[pos])
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

      def ascii_word_code?(code_points, pos)
        return false if pos.negative? || pos >= code_points.length

        ascii_word_char_class.include?(code_points[pos])
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
    end
  end
end
