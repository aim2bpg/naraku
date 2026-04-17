# frozen_string_literal: true

module NarakuRuby
  class CharClassBuildError < StandardError
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

  class CharClassBuilder
    CHAR_TYPE_TO_CPROP = {
      digit: 'Digit',
      word: 'Word',
      space: 'Space',
      hex_digit: 'XDigit',
    }.freeze

    POSIX_CLASS_TO_CPROP = {
      alnum: 'Alnum',
      alpha: 'Alpha',
      blank: 'Blank',
      cntrl: 'Cntrl',
      digit: 'Digit',
      graph: 'Graph',
      lower: 'Lower',
      print: 'Print',
      punct: 'Punct',
      space: 'Space',
      upper: 'Upper',
      xdigit: 'XDigit',
      ascii: 'ASCII',
      word: 'Word',
    }.freeze

    BuildState = Struct.new(:char_class, :expanded_strings, :ascii_case_fold_tracking, keyword_init: true)

    def initialize(node)
      @node = node
      @ascii_cprop_cache = {}
    end

    def build
      raise ArgumentError, 'node must be :char_class, :char_type, or :char_prop' unless %i[char_class char_type char_prop].include?(@node[:type])

      @strict = @node[:is_strict] || false
      @is_ignore_case = @node[:is_ignore_case]
      @fold_flags = @node[:fold_flags]
      @track_ascii = @is_ignore_case && !@strict

      state =
        if @node[:type] == :char_class
          build_char_class_core(
            unions: @node[:unions],
            is_positive: @track_ascii ? true : @node[:is_positive],
            source_node: @node
          )
        else
          build_item(@node)
        end

      if @track_ascii
        state = apply_case_fold(
          state,
          allow_multi_expand: @node[:is_positive]
        )
        state = negate_state(state) if (@node[:type] == :char_class) && !@node[:is_positive]
      end

      [state.char_class, state.expanded_strings]
    end

    private

    def build_char_class_core(unions:, is_positive:, source_node:)
      union_states = []

      unions.each do |union_node|
        union_state = build_union(union_node)
        union_states << union_state
      end

      state = union_states.empty? ? empty_state : union_states[0]

      union_states[1..]&.each do |next_state|
        if @strict && @is_ignore_case
          state = apply_case_fold(state)
          next_state = apply_case_fold(next_state)
        end
        state = intersect_states(state, next_state)
      end

      unless is_positive
        if @strict && @is_ignore_case
          state = apply_case_fold(state)
          raise build_error('cannot negate character class after multi-character case fold expansion', source_node) if state.expanded_strings.any?
        end
        state = negate_state(state)
      end

      state = apply_case_fold(state) if @strict && @is_ignore_case

      state
    end

    def build_union(union_node)
      items = union_node[:items]
      state = empty_state

      items.each do |item_node|
        item_state = build_item(item_node)
        state = union_states(state, item_state)
      end

      state
    end

    def build_item(item_node)
      state = case item_node[:type]
              when :code
                code = item_node[:code]
                BuildState.new(char_class: CharClass.new([code..code]), expanded_strings: Set.new)
              when :range
                BuildState.new(
                  char_class: CharClass.new([item_node[:begin_code]..item_node[:end_code]]),
                  expanded_strings: Set.new
                )
              when :char_type
                BuildState.new(char_class: char_class_from_char_type_node(item_node), expanded_strings: Set.new)
              when :char_prop
                BuildState.new(char_class: char_class_from_char_prop_node(item_node), expanded_strings: Set.new)
              when :posix_char_class
                BuildState.new(char_class: char_class_from_posix_class_node(item_node), expanded_strings: Set.new)
              when :nested_char_class
                build_char_class_core(
                  unions: item_node[:unions],
                  is_positive: item_node[:is_positive],
                  source_node: item_node
                )
              else
                raise build_error("unsupported char class item type: #{item_node[:type].inspect}", item_node)
              end

      ascii_case_fold_tracking =
        if @track_ascii
          if skip_ascii_case_fold_tracking?(item_node)
            CharClass.new
          else
            CharClass.new(state.char_class.ranges)
          end
        end

      BuildState.new(
        char_class: state.char_class,
        expanded_strings: state.expanded_strings,
        ascii_case_fold_tracking:
      )
    end

    def char_class_from_char_type_node(node)
      char_type = node[:char_type]
      cprop_name = CHAR_TYPE_TO_CPROP[char_type]
      raise build_error("unsupported char_type: #{char_type.inspect}", node) if cprop_name.nil?

      char_class = CharClass.new(NarakuRuby.cprop_code_range(cprop_name))
      char_class = char_class.intersect(CharClass.new([0..CharClass::ASCII_MAX])) if node[:is_ascii_only]
      node[:is_positive] ? char_class : char_class.negate
    end

    def char_class_from_char_prop_node(node)
      char_class = CharClass.new(NarakuRuby.cprop_code_range(node[:cprop]))
      node[:is_positive] ? char_class : char_class.negate
    end

    def char_class_from_posix_class_node(node)
      posix_class = node[:posix_char_class]
      cprop_name = POSIX_CLASS_TO_CPROP[posix_class]
      raise build_error("unsupported posix_char_class: #{posix_class.inspect}", node) if cprop_name.nil?

      char_class = CharClass.new(NarakuRuby.cprop_code_range(cprop_name))
      char_class = char_class.intersect(CharClass.new([0..CharClass::ASCII_MAX])) if node[:is_ascii_only]
      node[:is_positive] ? char_class : char_class.negate
    end

    def skip_ascii_case_fold_tracking?(item_node)
      case item_node[:type]
      when :posix_char_class
        item_node[:is_ascii_only] || %i[ascii word].include?(item_node[:posix_char_class])
      when :char_prop
        ascii_cprop?(item_node[:cprop])
      when :char_type
        item_node[:char_type] == :word
      else
        false
      end
    end

    def ascii_cprop?(cprop)
      @ascii_cprop_cache[cprop] ||= (NarakuRuby.cprop_code_range(cprop) == [0..CharClass::ASCII_MAX])
    end

    def apply_case_fold(state, allow_multi_expand: true)
      folded_char_class = CharClass.new(state.char_class.ranges)
      expanded_strings = state.expanded_strings.dup
      ascii_case_fold_tracking = state.ascii_case_fold_tracking

      NarakuRuby.iterate_case_fold(@fold_flags).each do |entry|
        code = entry[:code]
        next unless state.char_class.include?(code)

        folded_codes = entry[:folded_codes]
        if folded_codes.length > 1
          expanded_strings.add(folded_codes.pack('U*')) if allow_multi_expand
          next
        end

        folded_char_class.add(folded_codes[0])
      end

      unfolded_char_class = CharClass.new(folded_char_class.ranges)
      NarakuRuby.iterate_case_fold(@fold_flags).each do |entry|
        code = entry[:code]
        folded_codes = entry[:folded_codes]
        if folded_codes.length > 1
          unfolded_char_class.add(entry[:code]) if expanded_strings.include?(folded_codes.pack('U*'))
          next
        end

        folded_code = folded_codes[0]
        if code > CharClass::ASCII_MAX && folded_code <= CharClass::ASCII_MAX && ascii_case_fold_tracking && !ascii_case_fold_tracking.include?(folded_code)
          next
        end

        unfolded_char_class.add(code) if folded_char_class.include?(folded_code)
      end

      BuildState.new(
        char_class: unfolded_char_class,
        expanded_strings:,
        ascii_case_fold_tracking:
      )
    end

    def ascii_tracked_multi_expand?(folded_codes, ascii_case_fold_tracking)
      tracking = ascii_case_fold_tracking || CharClass.new
      folded_codes.none? do |code|
        code <= CharClass::ASCII_MAX && !tracking.include?(code)
      end
    end

    def union_states(left, right)
      BuildState.new(
        char_class: left.char_class.union(right.char_class),
        expanded_strings: left.expanded_strings | right.expanded_strings,
        ascii_case_fold_tracking: union_ascii_case_fold_tracking(
          left.ascii_case_fold_tracking,
          right.ascii_case_fold_tracking
        )
      )
    end

    def intersect_states(left, right)
      BuildState.new(
        char_class: left.char_class.intersect(right.char_class),
        expanded_strings: left.expanded_strings & right.expanded_strings,
        ascii_case_fold_tracking: intersect_ascii_case_fold_tracking(
          left.ascii_case_fold_tracking,
          right.ascii_case_fold_tracking
        )
      )
    end

    def negate_state(state)
      BuildState.new(
        char_class: state.char_class.negate,
        expanded_strings: Set.new,
        ascii_case_fold_tracking: state.ascii_case_fold_tracking&.negate
      )
    end

    def union_ascii_case_fold_tracking(left, right)
      return nil if left.nil? && right.nil?
      return CharClass.new(right.ranges) if left.nil?
      return CharClass.new(left.ranges) if right.nil?

      left.union(right)
    end

    def intersect_ascii_case_fold_tracking(left, right)
      return nil if left.nil? || right.nil?

      left.intersect(right)
    end

    def empty_state
      BuildState.new(
        char_class: CharClass.new,
        expanded_strings: Set.new,
        ascii_case_fold_tracking: @track_ascii ? CharClass.new : nil
      )
    end

    def build_error(message, node)
      offset = node[:span_offset]
      length = node[:span_length]
      CharClassBuildError.new(message, offset:, length:)
    end
  end
end
