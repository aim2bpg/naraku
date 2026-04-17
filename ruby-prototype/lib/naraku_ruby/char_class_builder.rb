# frozen_string_literal: true

module NarakuRuby
  class CharClassBuilderError < StandardError
    attr_reader :offset, :length

    def initialize(message, offset:, length:)
      super(message)
      @offset = offset
      @length = length
    end
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

    BuildState = Struct.new(:char_class, :expanded_strings, keyword_init: true)

    def initialize(node)
      @node = node
      @ascii_cprop_cache = {}
    end

    def build
      raise build_error('node must be :char_class', @node) unless node_type(@node) == :char_class

      strict = fetch_bool(@node, :is_strict)
      ignore_case = fetch_bool(@node, :is_ignore_case)
      fold_flags = fetch_fold_flags(@node)
      track_ascii = ignore_case && !strict

      state, ascii_state = build_char_class_core(
        unions: fetch_array(@node, :unions),
        is_positive: fetch_bool(@node, :is_positive),
        strict:,
        ignore_case:,
        fold_flags:,
        source_node: @node,
        track_ascii:
      )

      unless strict || !ignore_case
        state = apply_case_fold(state, fold_flags)
        state =
          if fetch_bool(@node, :is_positive)
            BuildState.new(
              char_class: state.char_class,
              expanded_strings: filter_expanded_strings(state.expanded_strings, ascii_state.char_class)
            )
          else
            BuildState.new(char_class: state.char_class, expanded_strings: Set.new)
          end
      end

      [state.char_class, state.expanded_strings]
    end

    private

    def build_char_class_core(unions:, is_positive:, strict:, ignore_case:, fold_flags:, source_node:, track_ascii:)
      union_states = []
      ascii_union_states = []

      unions.each do |union_node|
        union_state, ascii_union_state = build_union(
          union_node,
          strict:,
          ignore_case:,
          fold_flags:,
          track_ascii:
        )
        union_states << union_state
        ascii_union_states << ascii_union_state
      end

      state = union_states.empty? ? empty_state : union_states[0]
      ascii_state = ascii_union_states.empty? ? empty_state : ascii_union_states[0]

      union_states[1..]&.each do |next_state|
        if strict && ignore_case
          state = apply_case_fold(state, fold_flags)
          next_state = apply_case_fold(next_state, fold_flags)
        end
        state = intersect_states(state, next_state)
      end

      ascii_union_states[1..]&.each do |next_ascii_state|
        ascii_state = intersect_states(ascii_state, next_ascii_state)
      end

      unless is_positive
        if strict && ignore_case
          state = apply_case_fold(state, fold_flags)
          raise build_error('cannot negate character class after multi-character case fold expansion', source_node) if state.expanded_strings.any?
        end
        state = negate_state(state)
        ascii_state = negate_state(ascii_state)
      end

      state = apply_case_fold(state, fold_flags) if strict && ignore_case

      [state, ascii_state]
    end

    def build_union(union_node, strict:, ignore_case:, fold_flags:, track_ascii:)
      items = fetch_array(union_node, :items)
      state = empty_state
      ascii_state = empty_state

      items.each do |item_node|
        item_state, item_ascii_state = build_item(
          item_node,
          strict:,
          ignore_case:,
          fold_flags:,
          track_ascii:
        )
        state = union_states(state, item_state)
        ascii_state = union_states(ascii_state, item_ascii_state)
      end

      [state, ascii_state]
    end

    def build_item(item_node, strict:, ignore_case:, fold_flags:, track_ascii:)
      state = case node_type(item_node)
              when :code
                code = fetch_int(item_node, :code)
                BuildState.new(char_class: CharClass.new([code..code]), expanded_strings: Set.new)
              when :range
                BuildState.new(
                  char_class: CharClass.new([fetch_int(item_node, :begin_code)..fetch_int(item_node, :end_code)]),
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
                  unions: fetch_array(item_node, :unions),
                  is_positive: fetch_bool(item_node, :is_positive),
                  strict:,
                  ignore_case:,
                  fold_flags:,
                  source_node: item_node,
                  track_ascii:
                ).first
              else
                raise build_error("unsupported char class item type: #{node_type(item_node).inspect}", item_node)
              end

      ascii_state = if track_ascii && skip_ascii_case_fold_tracking?(item_node)
                      empty_state
                    else
                      duplicate_state(state)
                    end
      [state, ascii_state]
    end

    def char_class_from_char_type_node(node)
      char_type = fetch_symbol(node, :char_type)
      cprop_name = CHAR_TYPE_TO_CPROP[char_type]
      raise build_error("unsupported char_type: #{char_type.inspect}", node) if cprop_name.nil?

      char_class = CharClass.new(NarakuRuby.cprop_code_range(cprop_name))
      char_class = char_class.intersect(CharClass.new([0..CharClass::ASCII_MAX])) if fetch_bool(node, :is_ascii_only)
      fetch_bool(node, :is_positive) ? char_class : char_class.negate
    end

    def char_class_from_char_prop_node(node)
      char_class = CharClass.new(NarakuRuby.cprop_code_range(fetch_node_value(node, :cprop)))
      fetch_bool(node, :is_positive) ? char_class : char_class.negate
    end

    def char_class_from_posix_class_node(node)
      posix_class = fetch_symbol(node, :posix_char_class)
      cprop_name = POSIX_CLASS_TO_CPROP[posix_class]
      raise build_error("unsupported posix_char_class: #{posix_class.inspect}", node) if cprop_name.nil?

      char_class = CharClass.new(NarakuRuby.cprop_code_range(cprop_name))
      char_class = char_class.intersect(CharClass.new([0..CharClass::ASCII_MAX])) if fetch_bool(node, :is_ascii_only)
      fetch_bool(node, :is_positive) ? char_class : char_class.negate
    end

    def skip_ascii_case_fold_tracking?(item_node)
      case node_type(item_node)
      when :posix_char_class
        fetch_bool(item_node, :is_ascii_only) || %i[ascii word].include?(fetch_symbol(item_node, :posix_char_class))
      when :char_prop
        ascii_cprop?(fetch_node_value(item_node, :cprop))
      when :char_type
        fetch_symbol(item_node, :char_type) == :word
      else
        false
      end
    end

    def ascii_cprop?(cprop)
      @ascii_cprop_cache[cprop] ||= (NarakuRuby.cprop_code_range(cprop) == [0..CharClass::ASCII_MAX])
    end

    def apply_case_fold(state, fold_flags)
      folded_char_class, expanded_from_char_class = state.char_class.case_fold(*fold_flags)
      BuildState.new(
        char_class: folded_char_class,
        expanded_strings: state.expanded_strings | expanded_from_char_class
      )
    end

    def filter_expanded_strings(expanded_strings, ascii_case_fold_char_class)
      expanded_strings.select do |string|
        string.codepoints.none? do |code|
          code <= CharClass::ASCII_MAX && !ascii_case_fold_char_class.include?(code)
        end
      end
    end

    def union_states(left, right)
      BuildState.new(
        char_class: left.char_class.union(right.char_class),
        expanded_strings: left.expanded_strings | right.expanded_strings
      )
    end

    def intersect_states(left, right)
      BuildState.new(
        char_class: left.char_class.intersect(right.char_class),
        expanded_strings: left.expanded_strings & right.expanded_strings
      )
    end

    def negate_state(state)
      BuildState.new(char_class: state.char_class.negate, expanded_strings: Set.new)
    end

    def duplicate_state(state)
      BuildState.new(
        char_class: CharClass.new(state.char_class.ranges),
        expanded_strings: state.expanded_strings.dup
      )
    end

    def empty_state
      BuildState.new(char_class: CharClass.new, expanded_strings: Set.new)
    end

    def node_type(node)
      fetch_symbol(node, :type)
    end

    def fetch_symbol(node, key)
      value = fetch_node_value(node, key)
      value.is_a?(String) ? value.to_sym : value
    end

    def fetch_int(node, key)
      value = fetch_node_value(node, key)
      raise build_error("node key #{key} must be Integer", node) unless value.is_a?(Integer)

      value
    end

    def fetch_bool(node, key)
      !!fetch_node_value(node, key)
    end

    def fetch_array(node, key)
      value = fetch_node_value(node, key)
      raise build_error("node key #{key} must be Array", node) unless value.is_a?(Array)

      value
    end

    def fetch_fold_flags(node)
      Array(fetch_node_value(node, :fold_flags)).map { |flag| flag.is_a?(String) ? flag.to_sym : flag }
    end

    def fetch_node_value(node, key)
      return node[key] if node.key?(key)
      return node[key.to_s] if node.key?(key.to_s)

      raise build_error("missing node key: #{key}", node)
    end

    def build_error(message, node)
      offset = node[:span_offset] || node['span_offset'] || 0
      length = node[:span_length] || node['span_length'] || 0
      CharClassBuilderError.new(message, offset:, length:)
    end
  end
end
