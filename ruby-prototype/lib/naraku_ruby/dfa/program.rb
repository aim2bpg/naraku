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
      INVALID_UTF8_MESSAGE = 'invalid byte sequence in UTF-8'.freeze

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
      # Reset the lazy DFA cache when this many distinct NFA-state-sets have
      # accumulated, to cap memory use on complex patterns.
      LAZY_DFA_MAX_STATES = 1024

      def initialize(states:, initial_state:, num_capture_groups:, num_check_ids:, full_dfa: false, full_dfa_eval: false)
        @states = states
        @initial_state = initial_state
        @num_capture_groups = num_capture_groups
        @num_check_ids = num_check_ids
        @caps_size = (@num_capture_groups + 1) * 2
        @empty_caps = Array.new(@caps_size, -1).freeze
        @has_assertion = states.any? { |state| state.op == :assertion }

        @word_char_class = @ascii_word_char_class = nil
        @word_ascii_table = @ascii_word_ascii_table = nil
        @full_dfa_transition_codes = nil
        @full_dfa_transitions = nil
        @full_dfa_other_transitions = nil
        @full_dfa_matching = nil
        @full_dfa_start_index = nil
        @full_dfa_codes = nil
        @full_dfa_eval_matcher = nil

        # Lazy DFA cache: memoises (NFA-state-set, char) → next-NFA-state-set.
        # Enabled for assertion-free programs when full_dfa is not already in use.
        unless @has_assertion || full_dfa
          @lazy_dfa_transitions = {}  # state_set_key => {char_code => next_key}
          @lazy_dfa_state_sets  = {}  # state_set_key => Array<State> (reverse map)
          @lazy_dfa_accepting   = {}  # state_set_key => bool
          @lazy_dfa_start_key   = nil # computed on first match?
        end

        return unless full_dfa

        build_full_dfa_without_caps
        build_full_dfa_eval_matcher if full_dfa_eval
      end

      attr_reader :states, :initial_state

      def show_states
        @states.each do |state|
          puts format('%03d: %s', state.id, state.to_s)
        end
        nil
      end

      def match(string, pos = 0)
        validate_match_target!(string)
        caps = run_with_caps(string, pos)
        caps ? MatchData.new(string, caps) : nil
      end

      def match?(string, pos = 0)
        validate_match_target!(string)
        run_without_caps(string, pos)
      end

      private

      def run_with_caps(string, start_pos)
        if string.ascii_only?
          run_with_caps_ascii(string, start_pos)
        else
          run_with_caps_utf8(string, start_pos)
        end
      end

      def run_with_caps_ascii(string, start_pos)
        length = string.bytesize
        return nil if start_pos > length

        prev_code = start_pos.positive? ? string.getbyte(start_pos - 1) : nil
        curr_code = start_pos < length ? string.getbyte(start_pos) : nil
        next_code = start_pos + 1 < length ? string.getbyte(start_pos + 1) : nil
        pos = start_pos

        show_states if DEBUG

        threads = []
        next_threads = []
        visited_marks = Array.new(@num_check_ids, 0)
        visit_token = 1
        epsilon_closure_with_caps_stream(
          start_pos, pos, prev_code, curr_code, next_code, @initial_state, start_pos,
          Caps.new(@empty_caps.dup), 0, visited_marks, visit_token, threads
        )

        best_keep_pos = nil
        best_caps = nil

        while curr_code
          if DEBUG
            puts "pos: #{pos}"
            threads.each do |thread|
              p [thread.state.id, thread.state.op, thread.keep_pos, thread.caps]
            end
          end

          break if threads.any? && threads.first.state.op == :match

          code = curr_code
          visit_token += 1
          next_pos = pos + 1
          next_prev_code = code
          next_curr_code = next_code
          next_next_code = next_pos + 1 < length ? string.getbyte(next_pos + 1) : nil
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
              unless char_class_include?(state, code)
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
            epsilon_closure_with_caps_stream(
              start_pos, next_pos, next_prev_code, next_curr_code, next_next_code, state.next,
              thread.keep_pos, thread.caps, 0, visited_marks, visit_token, next_threads
            )
          end

          unless has_match_thread
            epsilon_closure_with_caps_stream(
              start_pos, next_pos, next_prev_code, next_curr_code, next_next_code, @initial_state,
              next_pos, Caps.new(@empty_caps.dup), 0, visited_marks, visit_token, next_threads
            )
          end

          threads, next_threads = next_threads, threads
          next_threads.clear
          curr_code = next_curr_code
          next_code = next_next_code
          pos = next_pos
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

      def run_with_caps_utf8(string, start_pos)
        seek = utf8_seek_byte_pos_with_prev(string, start_pos)
        return nil unless seek

        byte_pos, prev_code = seek
        bytesize = string.bytesize
        curr_decoded = byte_pos < bytesize ? utf8_decode_code_len(string, byte_pos) : nil
        curr_code = curr_decoded&.first
        curr_len = curr_decoded&.last
        next_decoded = curr_decoded && (byte_pos + curr_len < bytesize) ? utf8_decode_code_len(string, byte_pos + curr_len) : nil
        next_code = next_decoded&.first
        next_len = next_decoded&.last
        pos = start_pos

        show_states if DEBUG

        threads = []
        next_threads = []
        visited_marks = Array.new(@num_check_ids, 0)
        visit_token = 1
        epsilon_closure_with_caps_stream(
          start_pos, pos, prev_code, curr_code, next_code, @initial_state, start_pos,
          Caps.new(@empty_caps.dup), 0, visited_marks, visit_token, threads
        )

        best_keep_pos = nil
        best_caps = nil

        while curr_code
          if DEBUG
            puts "pos: #{pos}"
            threads.each do |thread|
              p [thread.state.id, thread.state.op, thread.keep_pos, thread.caps]
            end
          end

          break if threads.any? && threads.first.state.op == :match

          code = curr_code
          visit_token += 1
          next_pos = pos + 1
          next_prev_code = code
          next_curr_code = next_code
          next_curr_len = next_len
          next_byte_pos = byte_pos + curr_len
          next_next_decoded = (utf8_decode_code_len(string, next_byte_pos + next_curr_len) if next_curr_code && (next_byte_pos + next_curr_len < bytesize))
          next_next_code = next_next_decoded&.first
          next_next_len = next_next_decoded&.last
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
              unless char_class_include?(state, code)
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
            epsilon_closure_with_caps_stream(
              start_pos, next_pos, next_prev_code, next_curr_code, next_next_code, state.next,
              thread.keep_pos, thread.caps, 0, visited_marks, visit_token, next_threads
            )
          end

          unless has_match_thread
            epsilon_closure_with_caps_stream(
              start_pos, next_pos, next_prev_code, next_curr_code, next_next_code, @initial_state,
              next_pos, Caps.new(@empty_caps.dup), 0, visited_marks, visit_token, next_threads
            )
          end

          threads, next_threads = next_threads, threads
          next_threads.clear
          curr_code = next_curr_code
          curr_len = next_curr_len
          next_code = next_next_code
          next_len = next_next_len
          byte_pos = next_byte_pos
          pos = next_pos
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
        return run_without_caps_full_dfa(string, start_pos) if @full_dfa_transitions
        return run_without_caps_lazy_dfa(string, start_pos) if @lazy_dfa_transitions

        unless @has_assertion
          return run_without_caps_ascii_no_assertion(string, start_pos) if string.ascii_only?

          return run_without_caps_utf8_no_assertion(string, start_pos)
        end

        if string.ascii_only?
          run_without_caps_ascii_with_assertion(string, start_pos)
        else
          run_without_caps_utf8_with_assertion(string, start_pos)
        end
      end

      def run_without_caps_lazy_dfa(string, start_pos)
        if string.ascii_only?
          run_without_caps_lazy_dfa_ascii(string, start_pos)
        else
          run_without_caps_lazy_dfa_utf8(string, start_pos)
        end
      end

      def run_without_caps_lazy_dfa_ascii(string, start_pos)
        length = string.bytesize
        return false if start_pos > length

        key = (@lazy_dfa_start_key ||= init_lazy_dfa_start)
        return true if @lazy_dfa_accepting[key]

        pos = start_pos
        while pos < length
          key = lazy_dfa_step(key, string.getbyte(pos))
          return true if @lazy_dfa_accepting[key]

          pos += 1
        end
        @lazy_dfa_accepting[key]
      end

      def run_without_caps_lazy_dfa_utf8(string, start_pos)
        seek = utf8_seek_byte_pos(string, start_pos)
        return false unless seek

        byte_pos = seek
        bytesize = string.bytesize
        key = (@lazy_dfa_start_key ||= init_lazy_dfa_start)
        return true if @lazy_dfa_accepting[key]

        while byte_pos < bytesize
          code, len = utf8_decode_code_len(string, byte_pos)
          key = lazy_dfa_step(key, code)
          return true if @lazy_dfa_accepting[key]

          byte_pos += len
        end
        @lazy_dfa_accepting[key]
      end

      def init_lazy_dfa_start
        start_states = epsilon_closure_for_state_set_without_caps([@initial_state])
        key = build_state_set_key(start_states)
        @lazy_dfa_state_sets[key] = start_states
        @lazy_dfa_accepting[key]  = start_states.any? { |s| s.op == :match }
        key
      end

      def lazy_dfa_step(key, code)
        trans = (@lazy_dfa_transitions[key] ||= {})
        return trans[code] if trans.key?(code)

        from_states = @lazy_dfa_state_sets[key]
        next_entries = []
        from_states.each do |state|
          case state.op
          when :code
            next_entries << state.next if state.code == code
          when :char_class
            next_entries << state.next if char_class_include?(state, code)
          when :dot
            next_entries << state.next if code != 0x0A || state.newline
          end
        end
        next_entries << @initial_state

        next_states = epsilon_closure_for_state_set_without_caps(next_entries)
        next_key = build_state_set_key(next_states)
        unless @lazy_dfa_state_sets.key?(next_key)
          if @lazy_dfa_state_sets.size >= LAZY_DFA_MAX_STATES
            @lazy_dfa_transitions.clear
            @lazy_dfa_state_sets.clear
            @lazy_dfa_accepting.clear
            @lazy_dfa_start_key = nil
            # Re-seed the current and next states so this step remains valid.
            @lazy_dfa_state_sets[key] = from_states
            @lazy_dfa_accepting[key]  = from_states.any? { |s| s.op == :match }
            trans = (@lazy_dfa_transitions[key] ||= {})
          end
          @lazy_dfa_state_sets[next_key] = next_states
          @lazy_dfa_accepting[next_key]  = next_states.any? { |s| s.op == :match }
        end
        trans[code] = next_key
      end

      def run_without_caps_ascii_no_assertion(string, start_pos)
        threads = []
        next_threads = []
        visited_marks = Array.new(@num_check_ids, 0)
        visit_token = 1
        return true if epsilon_closure_without_caps_no_assertion(start_pos, @initial_state, 0, visited_marks, visit_token, threads)

        pos = start_pos
        length = string.bytesize
        while pos < length
          code = string.getbyte(pos)
          visit_token += 1
          threads.each do |state|
            case state.op
            when :match
              next if visited_marks[state.check_id] == visit_token

              return true
            when :code
              next unless state.code == code
            when :char_class
              next unless char_class_include?(state, code)
            when :dot
              next unless code != 0x0A || state.newline
            else
              raise "unexpected state: #{state}"
            end

            return true if epsilon_closure_without_caps_no_assertion(
              pos + 1, state.next, 0, visited_marks, visit_token, next_threads
            )
          end

          return true if epsilon_closure_without_caps_no_assertion(
            pos + 1, @initial_state, 0, visited_marks, visit_token, next_threads
          )

          threads, next_threads = next_threads, threads
          next_threads.clear
          pos += 1
        end

        threads.any? { |state| state.op == :match }
      end

      def run_without_caps_utf8_no_assertion(string, start_pos)
        seek = utf8_seek_byte_pos(string, start_pos)
        return false unless seek

        byte_pos = seek
        pos = start_pos
        bytesize = string.bytesize

        threads = []
        next_threads = []
        visited_marks = Array.new(@num_check_ids, 0)
        visit_token = 1
        return true if epsilon_closure_without_caps_no_assertion(pos, @initial_state, 0, visited_marks, visit_token, threads)

        while byte_pos < bytesize
          code, len = utf8_decode_code_len(string, byte_pos)

          visit_token += 1
          next_pos = pos + 1
          threads.each do |state|
            case state.op
            when :match
              next if visited_marks[state.check_id] == visit_token

              return true
            when :code
              next unless state.code == code
            when :char_class
              next unless char_class_include?(state, code)
            when :dot
              next unless code != 0x0A || state.newline
            else
              raise "unexpected state: #{state}"
            end

            return true if epsilon_closure_without_caps_no_assertion(
              next_pos, state.next, 0, visited_marks, visit_token, next_threads
            )
          end

          return true if epsilon_closure_without_caps_no_assertion(
            next_pos, @initial_state, 0, visited_marks, visit_token, next_threads
          )

          threads, next_threads = next_threads, threads
          next_threads.clear
          byte_pos += len
          pos = next_pos
        end

        threads.any? { |state| state.op == :match }
      end

      def run_without_caps_ascii_with_assertion(string, start_pos)
        length = string.bytesize
        return false if start_pos > length

        prev_code = start_pos.positive? ? string.getbyte(start_pos - 1) : nil
        curr_code = start_pos < length ? string.getbyte(start_pos) : nil
        next_code = start_pos + 1 < length ? string.getbyte(start_pos + 1) : nil
        pos = start_pos

        threads = []
        next_threads = []
        visited_marks = Array.new(@num_check_ids, 0)
        visit_token = 1
        return true if epsilon_closure_without_caps_with_assertion(
          start_pos, pos, prev_code, curr_code, next_code, @initial_state, 0, visited_marks, visit_token, threads
        )

        while curr_code
          code = curr_code
          visit_token += 1
          next_pos = pos + 1
          next_prev_code = code
          next_curr_code = next_code
          next_next_code = next_pos + 1 < length ? string.getbyte(next_pos + 1) : nil
          threads.each do |state|
            case state.op
            when :match
              next if visited_marks[state.check_id] == visit_token

              return true
            when :code
              next unless state.code == code
            when :char_class
              next unless char_class_include?(state, code)
            when :dot
              next unless code != 0x0A || state.newline
            else
              raise "unexpected state: #{state}"
            end

            return true if epsilon_closure_without_caps_with_assertion(
              start_pos, next_pos, next_prev_code, next_curr_code, next_next_code,
              state.next, 0, visited_marks, visit_token, next_threads
            )
          end

          return true if epsilon_closure_without_caps_with_assertion(
            start_pos, next_pos, next_prev_code, next_curr_code, next_next_code,
            @initial_state, 0, visited_marks, visit_token, next_threads
          )

          threads, next_threads = next_threads, threads
          next_threads.clear
          curr_code = next_curr_code
          next_code = next_next_code
          pos = next_pos
        end

        threads.any? { |state| state.op == :match }
      end

      def run_without_caps_utf8_with_assertion(string, start_pos)
        seek = utf8_seek_byte_pos_with_prev(string, start_pos)
        return false unless seek

        byte_pos, prev_code = seek
        bytesize = string.bytesize
        curr_decoded = byte_pos < bytesize ? utf8_decode_code_len(string, byte_pos) : nil
        curr_code = curr_decoded&.first
        curr_len = curr_decoded&.last
        next_decoded = curr_decoded && (byte_pos + curr_len < bytesize) ? utf8_decode_code_len(string, byte_pos + curr_len) : nil
        next_code = next_decoded&.first
        next_len = next_decoded&.last
        pos = start_pos

        threads = []
        next_threads = []
        visited_marks = Array.new(@num_check_ids, 0)
        visit_token = 1
        return true if epsilon_closure_without_caps_with_assertion(
          start_pos, pos, prev_code, curr_code, next_code, @initial_state, 0, visited_marks, visit_token, threads
        )

        while curr_code
          code = curr_code
          visit_token += 1
          next_pos = pos + 1
          next_prev_code = code
          next_curr_code = next_code
          next_curr_len = next_len
          next_byte_pos = byte_pos + curr_len
          next_next_decoded = (utf8_decode_code_len(string, next_byte_pos + next_curr_len) if next_curr_code && (next_byte_pos + next_curr_len < bytesize))
          next_next_code = next_next_decoded&.first
          next_next_len = next_next_decoded&.last
          threads.each do |state|
            case state.op
            when :match
              next if visited_marks[state.check_id] == visit_token

              return true
            when :code
              next unless state.code == code
            when :char_class
              next unless char_class_include?(state, code)
            when :dot
              next unless code != 0x0A || state.newline
            else
              raise "unexpected state: #{state}"
            end

            return true if epsilon_closure_without_caps_with_assertion(
              start_pos, next_pos, next_prev_code, next_curr_code, next_next_code,
              state.next, 0, visited_marks, visit_token, next_threads
            )
          end

          return true if epsilon_closure_without_caps_with_assertion(
            start_pos, next_pos, next_prev_code, next_curr_code, next_next_code,
            @initial_state, 0, visited_marks, visit_token, next_threads
          )

          threads, next_threads = next_threads, threads
          next_threads.clear
          curr_code = next_curr_code
          curr_len = next_curr_len
          next_code = next_next_code
          next_len = next_next_len
          byte_pos = next_byte_pos
          pos = next_pos
        end

        threads.any? { |state| state.op == :match }
      end

      def run_without_caps_full_dfa(string, start_pos)
        if string.ascii_only?
          return @full_dfa_eval_matcher.call(string, start_pos) if @full_dfa_eval_matcher

          return run_without_caps_full_dfa_ascii(string, start_pos)
        end

        seek = utf8_seek_byte_pos(string, start_pos)
        return false unless seek

        byte_pos = seek
        bytesize = string.bytesize
        state_index = @full_dfa_start_index

        while byte_pos < bytesize
          return true if @full_dfa_matching[state_index]

          code, len = utf8_decode_code_len(string, byte_pos)
          transition_index = @full_dfa_transition_codes[code]
          state_index = if transition_index
                          @full_dfa_transitions[state_index][transition_index]
                        else
                          @full_dfa_other_transitions[state_index]
                        end
          byte_pos += len
        end

        @full_dfa_matching[state_index]
      end

      def run_without_caps_full_dfa_ascii(string, start_pos)
        state_index = @full_dfa_start_index
        pos = start_pos
        length = string.bytesize

        while pos < length
          return true if @full_dfa_matching[state_index]

          code = string.getbyte(pos)
          transition_index = @full_dfa_transition_codes[code]
          state_index = if transition_index
                          @full_dfa_transitions[state_index][transition_index]
                        else
                          @full_dfa_other_transitions[state_index]
                        end
          pos += 1
        end

        @full_dfa_matching[state_index]
      end

      def epsilon_closure_with_caps_stream(start_pos, pos, prev_code, curr_code, next_code, state, keep_pos, caps, epsilon_bits, visited_marks,
                                           visit_token, next_threads)
        case state.op
        when :match, :code, :char_class, :dot
          return if visited_marks[state.check_id] == visit_token

          visited_marks[state.check_id] = visit_token
          next_threads << Thread.alloc(state, keep_pos, caps)
        when :jump
          epsilon_closure_with_caps_stream(start_pos, pos, prev_code, curr_code, next_code, state.next, keep_pos, caps, epsilon_bits, visited_marks,
                                           visit_token, next_threads)
        when :split
          epsilon_closure_with_caps_stream(start_pos, pos, prev_code, curr_code, next_code, state.next, keep_pos, caps.fork, epsilon_bits, visited_marks,
                                           visit_token, next_threads)
          epsilon_closure_with_caps_stream(start_pos, pos, prev_code, curr_code, next_code, state.split_next, keep_pos, caps, epsilon_bits, visited_marks,
                                           visit_token, next_threads)
        when :cap_begin
          caps.set_cap_begin(state.cap_num, pos)
          epsilon_closure_with_caps_stream(start_pos, pos, prev_code, curr_code, next_code, state.next, keep_pos, caps, epsilon_bits, visited_marks,
                                           visit_token, next_threads)
        when :cap_end
          caps.set_cap_end(state.cap_num, pos)
          epsilon_closure_with_caps_stream(start_pos, pos, prev_code, curr_code, next_code, state.next, keep_pos, caps, epsilon_bits, visited_marks,
                                           visit_token, next_threads)
        when :keep
          epsilon_closure_with_caps_stream(start_pos, pos, prev_code, curr_code, next_code, state.next, pos, caps, epsilon_bits, visited_marks,
                                           visit_token, next_threads)
        when :assertion
          return unless match_assertion_stream?(state.assertion_type, start_pos, pos, prev_code, curr_code, next_code)

          epsilon_closure_with_caps_stream(start_pos, pos, prev_code, curr_code, next_code, state.next, keep_pos, caps, epsilon_bits, visited_marks,
                                           visit_token, next_threads)
        when :check_visited
          return if visited_marks[state.check_id] == visit_token

          epsilon_closure_with_caps_stream(start_pos, pos, prev_code, curr_code, next_code, state.next, keep_pos, caps, epsilon_bits, visited_marks,
                                           visit_token, next_threads)
          visited_marks[state.check_id] = visit_token
        when :mark_epsilon
          epsilon_bits |= 1 << state.check_id
          epsilon_closure_with_caps_stream(start_pos, pos, prev_code, curr_code, next_code, state.next, keep_pos, caps, epsilon_bits, visited_marks,
                                           visit_token, next_threads)
        when :check_epsilon
          if epsilon_bits.nobits?(1 << state.check_id)
            epsilon_closure_with_caps_stream(start_pos, pos, prev_code, curr_code, next_code, state.next, keep_pos, caps, epsilon_bits, visited_marks,
                                             visit_token, next_threads)
          else
            epsilon_closure_with_caps_stream(start_pos, pos, prev_code, curr_code, next_code, state.split_next, keep_pos, caps, epsilon_bits, visited_marks,
                                             visit_token, next_threads)
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

      def epsilon_closure_without_caps_no_assertion(pos, state, epsilon_bits, visited_marks, visit_token, next_threads)
        case state.op
        when :match, :code, :char_class, :dot
          return false if visited_marks[state.check_id] == visit_token

          visited_marks[state.check_id] = visit_token
          next_threads << state
          state.op == :match
        when :jump, :cap_begin, :cap_end, :keep
          epsilon_closure_without_caps_no_assertion(pos, state.next, epsilon_bits, visited_marks, visit_token, next_threads)
        when :split
          epsilon_closure_without_caps_no_assertion(pos, state.next, epsilon_bits, visited_marks, visit_token, next_threads) ||
            epsilon_closure_without_caps_no_assertion(pos, state.split_next, epsilon_bits, visited_marks, visit_token, next_threads)
        when :check_visited
          return false if visited_marks[state.check_id] == visit_token

          matched = epsilon_closure_without_caps_no_assertion(pos, state.next, epsilon_bits, visited_marks, visit_token, next_threads)
          visited_marks[state.check_id] = visit_token
          matched
        when :mark_epsilon
          epsilon_bits |= 1 << state.check_id
          epsilon_closure_without_caps_no_assertion(pos, state.next, epsilon_bits, visited_marks, visit_token, next_threads)
        when :check_epsilon
          if epsilon_bits.nobits?(1 << state.check_id)
            epsilon_closure_without_caps_no_assertion(pos, state.next, epsilon_bits, visited_marks, visit_token, next_threads)
          else
            epsilon_closure_without_caps_no_assertion(pos, state.split_next, epsilon_bits, visited_marks, visit_token, next_threads)
          end
        else
          raise "unexpected state: #{state}"
        end
      end

      def epsilon_closure_without_caps_with_assertion(start_pos, pos, prev_code, curr_code, next_code, state, epsilon_bits, visited_marks, visit_token,
                                                      next_threads)
        case state.op
        when :match, :code, :char_class, :dot
          return false if visited_marks[state.check_id] == visit_token

          visited_marks[state.check_id] = visit_token
          next_threads << state
          state.op == :match
        when :jump, :cap_begin, :cap_end, :keep
          epsilon_closure_without_caps_with_assertion(start_pos, pos, prev_code, curr_code, next_code, state.next, epsilon_bits, visited_marks, visit_token,
                                                      next_threads)
        when :split
          epsilon_closure_without_caps_with_assertion(start_pos, pos, prev_code, curr_code, next_code, state.next, epsilon_bits, visited_marks, visit_token,
                                                      next_threads) ||
            epsilon_closure_without_caps_with_assertion(start_pos, pos, prev_code, curr_code, next_code, state.split_next, epsilon_bits, visited_marks,
                                                        visit_token, next_threads)
        when :assertion
          return false unless match_assertion_stream?(state.assertion_type, start_pos, pos, prev_code, curr_code, next_code)

          epsilon_closure_without_caps_with_assertion(start_pos, pos, prev_code, curr_code, next_code, state.next, epsilon_bits, visited_marks, visit_token,
                                                      next_threads)
        when :check_visited
          return false if visited_marks[state.check_id] == visit_token

          matched = epsilon_closure_without_caps_with_assertion(start_pos, pos, prev_code, curr_code, next_code, state.next, epsilon_bits, visited_marks,
                                                                visit_token, next_threads)
          visited_marks[state.check_id] = visit_token
          matched
        when :mark_epsilon
          epsilon_bits |= 1 << state.check_id
          epsilon_closure_without_caps_with_assertion(start_pos, pos, prev_code, curr_code, next_code, state.next, epsilon_bits, visited_marks, visit_token,
                                                      next_threads)
        when :check_epsilon
          if epsilon_bits.nobits?(1 << state.check_id)
            epsilon_closure_without_caps_with_assertion(start_pos, pos, prev_code, curr_code, next_code, state.next, epsilon_bits, visited_marks, visit_token,
                                                        next_threads)
          else
            epsilon_closure_without_caps_with_assertion(start_pos, pos, prev_code, curr_code, next_code, state.split_next, epsilon_bits, visited_marks,
                                                        visit_token, next_threads)
          end
        else
          raise "unexpected state: #{state}"
        end
      end

      def char_class_include?(state, code)
        if code <= 0x7F
          state.ascii_table[code]
        else
          state.char_class.fast_include?(code)
        end
      end

      def build_full_dfa_without_caps
        return unless full_dfa_compatible?

        codes = @states.filter_map { |state| state.code if state.op == :code }.uniq.sort
        @full_dfa_codes = codes
        transition_codes = {}
        codes.each_with_index { |code, index| transition_codes[code] = index }

        state_key_to_index = {}
        state_sets = []
        transitions = []
        other_transitions = []
        matching = []
        queue = []

        start_states = epsilon_closure_for_state_set_without_caps([@initial_state])
        start_key = build_state_set_key(start_states)
        state_key_to_index[start_key] = 0
        state_sets << start_states
        transitions << Array.new(codes.length, 0)
        other_transitions << 0
        matching << start_states.any? { |state| state.op == :match }
        queue << 0

        while queue.any?
          index = queue.shift
          from_states = state_sets[index]

          codes.each_with_index do |code, code_index|
            next_states = epsilon_closure_for_state_set_without_caps(states_after_code(from_states, code))
            next_index = add_full_dfa_state(state_key_to_index, state_sets, transitions, other_transitions, matching, queue, next_states, codes.length)
            transitions[index][code_index] = next_index
          end

          next_states = epsilon_closure_for_state_set_without_caps(states_after_other(from_states))
          other_transitions[index] = add_full_dfa_state(
            state_key_to_index, state_sets, transitions, other_transitions, matching, queue, next_states, codes.length
          )
        end

        @full_dfa_transition_codes = transition_codes.freeze
        @full_dfa_transitions = transitions.map(&:freeze).freeze
        @full_dfa_other_transitions = other_transitions.freeze
        @full_dfa_matching = matching.freeze
        @full_dfa_start_index = 0
      end

      def build_full_dfa_eval_matcher
        return unless @full_dfa_transitions
        return if @full_dfa_transitions.empty?

        # Keep generated code size bounded to avoid excessive compile overhead.
        return if @full_dfa_transitions.length > 512
        return if @full_dfa_codes.length > 256

        lines = []
        lines << 'lambda do |string, start_pos|'
        lines << "  state_index = #{@full_dfa_start_index}"
        lines << '  pos = start_pos'
        lines << '  limit = string.bytesize'
        lines << '  while pos < limit'
        lines << '    case state_index'

        @full_dfa_transitions.each_with_index do |row, state_index|
          lines << "    when #{state_index}"
          if @full_dfa_matching[state_index]
            lines << '      return true'
            next
          end
          lines << '      code = string.getbyte(pos)'
          lines << '      state_index = case code'
          row.each_with_index do |next_state_index, code_index|
            lines << "      when #{@full_dfa_codes[code_index]} then #{next_state_index}"
          end
          lines << "      else #{@full_dfa_other_transitions[state_index]}"
          lines << '      end'
          lines << '      pos += 1'
        end

        lines << '    else'
        lines << "      raise \"invalid full_dfa state index: \#{state_index}\""
        lines << '    end'
        lines << '  end'
        lines << '  case state_index'
        @full_dfa_matching.each_with_index do |is_match, state_index|
          lines << "  when #{state_index} then true" if is_match
        end
        lines << '  else false'
        lines << '  end'
        lines << 'end'

        @full_dfa_eval_matcher = eval(lines.join("\n"), binding, __FILE__, __LINE__) # rubocop:disable Security/Eval
      end

      def add_full_dfa_state(state_key_to_index, state_sets, transitions, other_transitions, matching, queue, states, code_size)
        key = build_state_set_key(states)
        existing = state_key_to_index[key]
        return existing if existing

        index = state_sets.length
        state_key_to_index[key] = index
        state_sets << states
        transitions << Array.new(code_size, 0)
        other_transitions << 0
        matching << states.any? { |state| state.op == :match }
        queue << index
        index
      end

      def states_after_code(states, code)
        entries = []
        states.each do |state|
          case state.op
          when :code
            entries << state.next if state.code == code
          when :match
            return [state]
          end
        end
        entries << @initial_state
        entries
      end

      def states_after_other(states)
        entries = []
        states.each do |state|
          case state.op
          when :code
            # no-op: `other` excludes all literal code transitions
          when :match
            return [state]
          end
        end
        entries << @initial_state
        entries
      end

      def epsilon_closure_for_state_set_without_caps(entry_states)
        visited_marks = Array.new(@num_check_ids, 0)
        visit_token = 1
        next_states = []
        entry_states.each do |state|
          next unless state

          epsilon_closure_without_caps([], 0, 0, state, 0, visited_marks, visit_token, next_states)
        end
        next_states
      end

      def full_dfa_compatible?
        @states.all? do |state|
          case state.op
          when :match, :code, :jump, :split, :cap_begin, :cap_end, :keep, :check_visited, :mark_epsilon, :check_epsilon
            true
          else
            false
          end
        end
      end

      def build_state_set_key(states)
        ids = states.filter_map(&:id).uniq.sort
        ids.pack('N*')
      end

      def utf8_seek_byte_pos(string, target_pos)
        byte_pos = 0
        pos = 0
        bytesize = string.bytesize
        while pos < target_pos && byte_pos < bytesize
          byte_pos += utf8_char_length_at(string, byte_pos)
          pos += 1
        end
        return nil if pos < target_pos

        byte_pos
      end

      def utf8_seek_byte_pos_with_prev(string, target_pos)
        byte_pos = 0
        pos = 0
        bytesize = string.bytesize
        prev_code = nil
        while pos < target_pos && byte_pos < bytesize
          code, len = utf8_decode_code_len(string, byte_pos)
          prev_code = code
          byte_pos += len
          pos += 1
        end
        return nil if pos < target_pos

        [byte_pos, prev_code]
      end

      def utf8_char_length_at(string, byte_pos)
        _, len = utf8_decode_code_len(string, byte_pos)
        len
      end

      def utf8_decode_code_len(string, byte_pos)
        b0 = string.getbyte(byte_pos)
        return [b0, 1] if b0 < 0x80

        if b0.between?(0xC2, 0xDF)
          b1 = string.getbyte(byte_pos + 1)
          if continuation_byte?(b1)
            code = ((b0 & 0x1F) << 6) | (b1 & 0x3F)
            return [code, 2] if code >= 0x80
          end
          raise_invalid_utf8!
        end

        if b0.between?(0xE0, 0xEF)
          b1 = string.getbyte(byte_pos + 1)
          b2 = string.getbyte(byte_pos + 2)
          if continuation_byte?(b1) && continuation_byte?(b2)
            code = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F)
            return [code, 3] if code >= 0x800 && !(0xD800..0xDFFF).cover?(code)
          end
          raise_invalid_utf8!
        end

        if b0.between?(0xF0, 0xF4)
          b1 = string.getbyte(byte_pos + 1)
          b2 = string.getbyte(byte_pos + 2)
          b3 = string.getbyte(byte_pos + 3)
          if continuation_byte?(b1) && continuation_byte?(b2) && continuation_byte?(b3)
            code = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F)
            return [code, 4] if code.between?(0x10000, 0x10FFFF)
          end
          raise_invalid_utf8!
        end

        raise_invalid_utf8!
      end

      def continuation_byte?(byte)
        byte && (byte & 0xC0) == 0x80
      end

      def raise_invalid_utf8!
        raise ArgumentError, INVALID_UTF8_MESSAGE
      end

      def validate_match_target!(string)
        return if string.valid_encoding?

        raise ArgumentError, INVALID_UTF8_MESSAGE
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

      def match_assertion_stream?(assertion_type, start_pos, pos, prev_code, curr_code, next_code)
        case assertion_type
        when :begin_of_line
          pos.zero? || (prev_code == 0x0A && !curr_code.nil?)
        when :end_of_line
          curr_code.nil? || curr_code == 0x0A
        when :begin_of_string
          pos.zero?
        when :end_of_string_strict
          curr_code.nil?
        when :end_of_string_loose
          curr_code.nil? || (curr_code == 0x0A && next_code.nil?)
        when :begin_of_matching
          pos == start_pos
        when :word_boundary
          word_code_from_code(prev_code) != word_code_from_code(curr_code)
        when :non_word_boundary
          word_code_from_code(prev_code) == word_code_from_code(curr_code)
        when :ascii_word_boundary
          ascii_word_code_from_code(prev_code) != ascii_word_code_from_code(curr_code)
        when :non_ascii_word_boundary
          ascii_word_code_from_code(prev_code) == ascii_word_code_from_code(curr_code)
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

      def word_code_from_code(code)
        return false unless code
        return word_ascii_table[code] if code <= 0x7F

        word_char_class.fast_include?(code)
      end

      def ascii_word_code_from_code(code)
        return false unless code && code <= 0x7F

        ascii_word_ascii_table[code]
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
