module Parser
  UINT32_MAX = (2**32) - 1

  class TestParser < Mtest::Test
    def parse(pattern, encoding: Naraku::Encoding::UTF_8, **options)
      parser = Naraku::Parser.new(encoding, pattern, **options)
      node = parser.parse
      result = node.to_h
      assert_span_consistency(pattern, result)
      result
    end

    def assert_parse_error(pattern, message, offset:, length:, encoding: Naraku::Encoding::UTF_8, **options)
      parser = Naraku::Parser.new(encoding, pattern, **options)
      begin
        parser.parse
      rescue Naraku::ParseError => ex
        expected_message =
          if length > 0
            "#{message} (at span #{offset}...#{offset + length})"
          else
            "#{message} (at offset #{offset})"
          end
        assert_equal expected_message, ex.message.lines.first.chomp
        assert_equal offset, ex.offset
        assert_equal length, ex.length
        return
      end

      assert false, "Expected Naraku::ParseError to be raised for #{pattern.inspect}"
    end

    def assert_span_consistency(pattern, root)
      pattern_len = pattern.bytesize
      assert_node_span(root, pattern_len, 0, pattern_len)
    end

    def assert_span_fields(obj, kind, pattern_len, parent_offset, parent_end)
      assert obj.key?(:span_offset), "#{kind} is missing span_offset"
      assert obj.key?(:span_length), "#{kind} is missing span_length"
      offset = obj[:span_offset]
      length = obj[:span_length]
      assert offset.is_a?(Integer), "#{kind} span_offset must be an Integer"
      assert length.is_a?(Integer), "#{kind} span_length must be an Integer"
      assert offset >= 0, "#{kind} span_offset must be non-negative"
      assert length >= 0, "#{kind} span_length must be non-negative"

      span_end = offset + length
      assert span_end <= pattern_len, "#{kind} span must be within pattern length"
      assert offset >= parent_offset, "#{kind} span_offset must be inside parent span"
      assert span_end <= parent_end, "#{kind} span_end must be inside parent span"

      [offset, span_end]
    end

    def assert_node_span(node, pattern_len, parent_offset, parent_end)
      kind = "node(#{node[:type]})"
      offset, span_end = assert_span_fields(node, kind, pattern_len, parent_offset, parent_end)

      case node[:type]
      when :char_class
        node[:unions].each do |char_class_union|
          assert_char_class_union_span(char_class_union, pattern_len, offset, span_end)
        end
      when :assertion, :quantifier, :group, :atomic, :absence
        assert_node_span(node[:child], pattern_len, offset, span_end) if node[:child]
      when :conditional
        assert_node_span(node[:yes_child], pattern_len, offset, span_end)
        assert_node_span(node[:no_child], pattern_len, offset, span_end) if node[:no_child]
      when :concat, :alt
        node[:children].each do |child|
          assert_node_span(child, pattern_len, offset, span_end)
        end
      end
    end

    def assert_char_class_union_span(char_class_union, pattern_len, parent_offset, parent_end)
      offset, span_end = assert_span_fields(char_class_union, 'char_class_union', pattern_len, parent_offset, parent_end)
      char_class_union[:items].each do |item|
        assert_char_class_item_span(item, pattern_len, offset, span_end)
      end
    end

    def assert_char_class_item_span(item, pattern_len, parent_offset, parent_end)
      offset, span_end = assert_span_fields(item, "char_class_item(#{item[:type]})", pattern_len, parent_offset, parent_end)
      return unless item[:type] == :nested_char_class

      item[:unions].each do |char_class_union|
        assert_char_class_union_span(char_class_union, pattern_len, offset, span_end)
      end
    end

    # ========================================================================
    #
    # Empty pattern:
    #
    # ========================================================================

    def test_empty_pattern
      result = parse('')
      assert_equal :concat, result[:type]
      assert_equal [], result[:children]
    end

    # ========================================================================
    #
    # Literals:
    #
    # ========================================================================

    def test_single_literal
      result = parse('a')
      assert_equal :literal, result[:type]
      assert_equal 'a', result[:buf]
    end

    def test_multi_char_literal
      result = parse('abc')
      assert_equal :literal, result[:type]
      assert_equal 'abc', result[:buf]
    end

    def test_escaped_special_char_as_literal
      result = parse('\*')
      assert_equal :literal, result[:type]
      assert_equal '*', result[:buf]
    end

    def test_escaped_plus_as_literal
      result = parse('\+')
      assert_equal :literal, result[:type]
      assert_equal '+', result[:buf]
    end

    def test_escaped_question_as_literal
      result = parse('\?')
      assert_equal :literal, result[:type]
      assert_equal '?', result[:buf]
    end

    def test_escaped_dot_as_literal
      result = parse('\.')
      assert_equal :literal, result[:type]
      assert_equal '.', result[:buf]
    end

    def test_escaped_pipe_as_literal
      result = parse('\|')
      assert_equal :literal, result[:type]
      assert_equal '|', result[:buf]
    end

    def test_escaped_caret_as_literal
      result = parse('\^')
      assert_equal :literal, result[:type]
      assert_equal '^', result[:buf]
    end

    def test_escaped_dollar_as_literal
      result = parse('\$')
      assert_equal :literal, result[:type]
      assert_equal '$', result[:buf]
    end

    def test_escaped_backslash_as_literal
      result = parse('\\\\')
      assert_equal :literal, result[:type]
      assert_equal '\\', result[:buf]
    end

    def test_escaped_brace_as_literal
      result = parse('\{')
      assert_equal :literal, result[:type]
      assert_equal '{', result[:buf]
    end

    def test_multibyte_literal
      result = parse('あ')
      assert_equal :literal, result[:type]
      assert_equal 'あ', result[:buf]
    end

    def test_adjacent_literals_with_multibyte
      result = parse('aあb')
      assert_equal :literal, result[:type]
      assert_equal 'aあb', result[:buf]
    end

    def test_node_span_for_literal
      result = parse('abc')
      assert_equal 0, result[:span_offset]
      assert_equal 3, result[:span_length]
    end

    # ========================================================================
    #
    # Dot:
    #
    # ========================================================================

    def test_dot
      result = parse('.')
      assert_equal :dot, result[:type]
      assert_equal false, result[:allows_newline]
    end

    def test_dot_allows_newline
      result = parse('.', dot_allows_newline: true)
      assert_equal :dot, result[:type]
      assert_equal true, result[:allows_newline]
    end

    # ========================================================================
    #
    # Assertions:
    #
    # ========================================================================

    def test_assertion_begin_of_line
      result = parse('^')
      assert_equal :assertion, result[:type]
      assert_equal :begin_of_line, result[:assertion_type]
      assert_nil result[:child]
    end

    def test_assertion_end_of_line
      result = parse('$')
      assert_equal :assertion, result[:type]
      assert_equal :end_of_line, result[:assertion_type]
      assert_nil result[:child]
    end

    def test_assertion_word_boundary
      result = parse('\b')
      assert_equal :assertion, result[:type]
      assert_equal :word_boundary, result[:assertion_type]
    end

    def test_assertion_non_word_boundary
      result = parse('\B')
      assert_equal :assertion, result[:type]
      assert_equal :non_word_boundary, result[:assertion_type]
    end

    def test_assertion_begin_of_string
      result = parse('\A')
      assert_equal :assertion, result[:type]
      assert_equal :begin_of_string, result[:assertion_type]
    end

    def test_assertion_end_of_string_strict
      result = parse('\z')
      assert_equal :assertion, result[:type]
      assert_equal :end_of_string_strict, result[:assertion_type]
    end

    def test_assertion_end_of_string_loose
      result = parse('\Z')
      assert_equal :assertion, result[:type]
      assert_equal :end_of_string_loose, result[:assertion_type]
    end

    def test_assertion_begin_of_matching
      result = parse('\G')
      assert_equal :assertion, result[:type]
      assert_equal :begin_of_matching, result[:assertion_type]
    end

    # ========================================================================
    #
    # Character types:
    #
    # ========================================================================

    def test_char_type_digit
      result = parse('\d')
      assert_equal :char_type, result[:type]
      assert_equal :digit, result[:char_type]
      assert_equal true, result[:is_positive]
    end

    def test_char_type_non_digit
      result = parse('\D')
      assert_equal :char_type, result[:type]
      assert_equal :digit, result[:char_type]
      assert_equal false, result[:is_positive]
    end

    def test_char_type_word
      result = parse('\w')
      assert_equal :char_type, result[:type]
      assert_equal :word, result[:char_type]
      assert_equal true, result[:is_positive]
    end

    def test_char_type_non_word
      result = parse('\W')
      assert_equal :char_type, result[:type]
      assert_equal :word, result[:char_type]
      assert_equal false, result[:is_positive]
    end

    def test_char_type_space
      result = parse('\s')
      assert_equal :char_type, result[:type]
      assert_equal :space, result[:char_type]
      assert_equal true, result[:is_positive]
    end

    def test_char_type_non_space
      result = parse('\S')
      assert_equal :char_type, result[:type]
      assert_equal :space, result[:char_type]
      assert_equal false, result[:is_positive]
    end

    def test_char_type_hex_digit
      result = parse('\h')
      assert_equal :char_type, result[:type]
      assert_equal :hex_digit, result[:char_type]
      assert_equal true, result[:is_positive]
    end

    def test_char_type_non_hex_digit
      result = parse('\H')
      assert_equal :char_type, result[:type]
      assert_equal :hex_digit, result[:char_type]
      assert_equal false, result[:is_positive]
    end

    def test_char_type_ascii_only_option
      result = parse('\d', char_type_is_ascii_only: true)
      assert_equal :char_type, result[:type]
      assert_equal true, result[:is_ascii_only]
    end

    def test_char_type_ignore_case_option
      result = parse('\w', is_ignore_case: true)
      assert_equal :char_type, result[:type]
      assert_equal true, result[:is_ignore_case]
    end

    # ========================================================================
    #
    # Unicode escapes:
    #
    # ========================================================================

    def test_unicode_escape_fixed
      result = parse('\u0061')
      assert_equal :literal, result[:type]
      assert_equal 'a', result[:buf]

      result = parse('\u3042')
      assert_equal :literal, result[:type]
      assert_equal 'あ', result[:buf]
    end

    def test_unicode_escape_variable
      result = parse('\u{61}')
      assert_equal :literal, result[:type]
      assert_equal 'a', result[:buf]

      result = parse('\u{3042}')
      assert_equal :literal, result[:type]
      assert_equal 'あ', result[:buf]

      result = parse('\u{1F308}')
      assert_equal :literal, result[:type]
      assert_equal "\u{1F308}", result[:buf]
    end

    def test_unicode_escape_multiple
      result = parse('\u{61 62 63}')
      assert_equal :literal, result[:type]
      assert_equal 'abc', result[:buf]

      result = parse('\u{3042 3044}')
      assert_equal :literal, result[:type]
      assert_equal 'あい', result[:buf]
    end

    def test_unicode_escape_whitespace
      result = parse('\u{  61  }')
      assert_equal :literal, result[:type]
      assert_equal 'a', result[:buf]

      result = parse("\\u{61\t62}")
      assert_equal :literal, result[:type]
      assert_equal 'ab', result[:buf]
    end

    def test_unicode_escape_large_code_point
      # U+10FFFF is the maximum valid Unicode code point.
      result = parse('\u{10FFFF}')
      assert_equal :literal, result[:type]
      assert_equal "\u{10FFFF}", result[:buf]

      # U+110000 is out of range for UTF-8.
      assert_parse_error('\u{110000}', 'code point is out of range', offset: 0, length: 10)
    end

    def test_unicode_escape_surrogate
      # Surrogate code points are invalid in UTF-8.
      assert_parse_error('\u{D800}', 'invalid code point', offset: 0, length: 8)
      assert_parse_error('\u{DFFF}', 'invalid code point', offset: 0, length: 8)
    end

    def test_unicode_escape_encoding_constraint
      # U+0080 is out of range for US-ASCII.
      assert_parse_error('\u0080', 'Unicode escape sequence in non-Unicode encoding', offset: 2, length: 0, encoding: Naraku::Encoding::US_ASCII)
      assert_parse_error('\u{80}', 'Unicode escape sequence in non-Unicode encoding', offset: 2, length: 0, encoding: Naraku::Encoding::US_ASCII)
    end

    def test_unicode_escape_errors
      # Trailing \u
      assert_parse_error('\u', 'unclosed Unicode escape sequence brace', offset: 2, length: 0)
      # Too short fixed escape
      assert_parse_error('\u123', 'incomplete Unicode escape sequence', offset: 5, length: 0)
      # Invalid hex digit
      assert_parse_error('\u123G', 'incomplete Unicode escape sequence', offset: 5, length: 0)
      # Unclosed brace
      assert_parse_error('\u{61', 'unclosed Unicode escape sequence brace', offset: 5, length: 0)
      # Invalid hex in brace
      assert_parse_error('\u{G}', 'invalid Unicode escape sequence', offset: 3, length: 0)
      # Empty brace
      assert_parse_error('\u{}', 'empty Unicode escape sequence brace', offset: 3, length: 0)
    end

    # ========================================================================
    #
    # Escape sequences:
    #
    # ========================================================================

    def test_trailing_backslash
      assert_parse_error('\\', 'incomplete escape sequence', offset: 1, length: 0)
    end

    def test_backslash_and_newline
      result = parse("\\\n")
      assert_equal :concat, result[:type]
      assert_equal 0, result[:children].size

      result = parse("\\\r\n")
      assert_equal :concat, result[:type]
      assert_equal 0, result[:children].size
    end

    def test_hex_escape
      result = parse('\x61')
      assert_equal :literal, result[:type]
      assert_equal 'a', result[:buf]

      # Single hex digit
      result = parse('\x1')
      assert_equal :literal, result[:type]
      assert_equal "\x01", result[:buf]

      result = parse('\x7F')
      assert_equal :literal, result[:type]
      assert_equal "\x7F", result[:buf]
    end

    def test_octal_escape
      result = parse('\0')
      assert_equal :literal, result[:type]
      assert_equal "\0", result[:buf]

      result = parse('\012')
      assert_equal :literal, result[:type]
      assert_equal "\n", result[:buf]

      result = parse('\123')
      assert_equal :literal, result[:type]
      assert_equal "S", result[:buf]

      # \07 is octal
      result = parse('\07')
      assert_equal :literal, result[:type]
      assert_equal "\a", result[:buf]
    end

    def test_meta_control_escape
      result = parse('\M-a', encoding: Naraku::Encoding::ASCII_8BIT)
      assert_equal :literal, result[:type]
      assert_equal "\xe1", result[:buf].bytes.map { |b| b.chr }.join

      result = parse('\C-a', encoding: Naraku::Encoding::ASCII_8BIT)
      assert_equal :literal, result[:type]
      assert_equal "\x01", result[:buf]

      result = parse('\ca', encoding: Naraku::Encoding::ASCII_8BIT)
      assert_equal :literal, result[:type]
      assert_equal "\x01", result[:buf]

      result = parse('\C-?', encoding: Naraku::Encoding::ASCII_8BIT)
      assert_equal :literal, result[:type]
      assert_equal "\x7F", result[:buf]

      result = parse('\c?', encoding: Naraku::Encoding::ASCII_8BIT)
      assert_equal :literal, result[:type]
      assert_equal "\x7F", result[:buf]

      result = parse('\C-\q', encoding: Naraku::Encoding::ASCII_8BIT)
      assert_equal :literal, result[:type]
      assert_equal "\x11", result[:buf]

      result = parse('\c\q', encoding: Naraku::Encoding::ASCII_8BIT)
      assert_equal :literal, result[:type]
      assert_equal "\x11", result[:buf]

      result = parse('\M-\C-a', encoding: Naraku::Encoding::ASCII_8BIT)
      assert_equal :literal, result[:type]
      assert_equal "\x81", result[:buf].bytes.map { |b| b.chr }.join

      result = parse('\M-\ca', encoding: Naraku::Encoding::ASCII_8BIT)
      assert_equal :literal, result[:type]
      assert_equal "\x81", result[:buf].bytes.map { |b| b.chr }.join
    end

    def test_standard_escapes
      assert_equal "\n", parse('\n')[:buf]
      assert_equal "\t", parse('\t')[:buf]
      assert_equal "\r", parse('\r')[:buf]
      assert_equal "\f", parse('\f')[:buf]
      assert_equal "\v", parse('\v')[:buf]
      assert_equal "\a", parse('\a')[:buf]
      assert_equal "\e", parse('\e')[:buf]
    end

    def test_multibyte_escaped_sequence
      # UTF-8 encoded 'あ'
      result = parse('\xe3\x81\x82')
      assert_equal :literal, result[:type]
      assert_equal 'あ', result[:buf]

      result = parse('\343\201\202')
      assert_equal :literal, result[:type]
      assert_equal 'あ', result[:buf]
    end

    def test_escape_errors
      # Missing hex digits
      assert_parse_error('\x', 'incomplete \x escape sequence', offset: 2, length: 0)
      # Invalid hex digit
      assert_parse_error('\xG', 'incomplete \x escape sequence', offset: 2, length: 0)
      # Missing meta character
      assert_parse_error('\M', 'incomplete \M- escape sequence', offset: 2, length: 0)
      assert_parse_error('\M-', 'incomplete \M- escape sequence', offset: 3, length: 0)
      assert_parse_error('\c', 'incomplete \c/\C- escape sequence', offset: 2, length: 0)
      assert_parse_error('\C-', 'incomplete \c/\C- escape sequence', offset: 3, length: 0)
      # Duplicate prefixes
      assert_parse_error('\M-\M-a', 'duplicate \M- escape sequence', offset: 5, length: 0)
      assert_parse_error('\C-\C-a', 'duplicate \c/\C- escape sequence', offset: 5, length: 0)
      # Invalid control/meta character
      assert_parse_error('\C-あ', 'invalid code in \c/\C- escape sequence', offset: 6, length: 0)
      assert_parse_error('\cあ', 'invalid code in \c/\C- escape sequence', offset: 5, length: 0)
      assert_parse_error('\M-あ', 'invalid code in \M- escape sequence', offset: 6, length: 0)
      # Incomplete multibyte sequence (only first byte of 'あ')
      assert_parse_error('\xe3', 'incomplete escaped byte sequence', offset: 4, length: 0)
      assert_parse_error('\xe3\x81', 'incomplete escaped byte sequence', offset: 8, length: 0)
      # Invalid multibyte sequence (surrogate code point)
      assert_parse_error('\xED\xA0\x80', 'invalid escaped byte sequence', offset: 12, length: 0)
    end

    # ========================================================================
    #
    # Unicode properties:
    #
    # ========================================================================

    def test_char_prop
      # Positive property \p{...}
      result = parse('\p{Lu}')
      assert_equal :char_prop, result[:type]
      assert_equal true, result[:is_positive]
      assert_equal Naraku::Encoding.name_to_cprop('Lu'), result[:cprop]

      result = parse('\p{L}')
      assert_equal :char_prop, result[:type]
      assert_equal true, result[:is_positive]
      assert_equal Naraku::Encoding.name_to_cprop('L'), result[:cprop]

      result = parse('\p{Digit}')
      assert_equal :char_prop, result[:type]
      assert_equal true, result[:is_positive]
      assert_equal Naraku::Encoding.name_to_cprop('Digit'), result[:cprop]

      # Negative property \P{...}
      result = parse('\P{Lu}')
      assert_equal :char_prop, result[:type]
      assert_equal false, result[:is_positive]
      assert_equal Naraku::Encoding.name_to_cprop('Lu'), result[:cprop]

      result = parse('\P{Digit}')
      assert_equal :char_prop, result[:type]
      assert_equal false, result[:is_positive]
      assert_equal Naraku::Encoding.name_to_cprop('Digit'), result[:cprop]

      # Negative property \p{^...}
      result = parse('\p{^Lu}')
      assert_equal :char_prop, result[:type]
      assert_equal false, result[:is_positive]
      assert_equal Naraku::Encoding.name_to_cprop('Lu'), result[:cprop]

      result = parse('\P{^Digit}')
      assert_equal :char_prop, result[:type]
      assert_equal true, result[:is_positive]
      assert_equal Naraku::Encoding.name_to_cprop('Digit'), result[:cprop]

      # Unicode escapes in property names
      result = parse('\p{\u004c\u0075}')
      assert_equal :char_prop, result[:type]
      assert_equal true, result[:is_positive]
      assert_equal Naraku::Encoding.name_to_cprop('Lu'), result[:cprop]

      result = parse('\p{\u{4c 75}}')
      assert_equal :char_prop, result[:type]
      assert_equal true, result[:is_positive]
      assert_equal Naraku::Encoding.name_to_cprop('Lu'), result[:cprop]

      result = parse('\p{\u{4c}u}')
      assert_equal :char_prop, result[:type]
      assert_equal true, result[:is_positive]
      assert_equal Naraku::Encoding.name_to_cprop('Lu'), result[:cprop]
    end

    def test_char_prop_missing_brace
      result = parse('\p')
      assert_equal :literal, result[:type]
      assert_equal 'p', result[:buf]

      result = parse('\P')
      assert_equal :literal, result[:type]
      assert_equal 'P', result[:buf]
    end

    def test_char_prop_errors
      # Unclosed brace
      assert_parse_error('\p{Lu', 'unclosed character property escape sequence brace', offset: 5, length: 0)
      # Empty property name
      assert_parse_error('\p{}', 'empty character property name', offset: 3, length: 0)
      # Invalid property name
      assert_parse_error('\p{InvalidProperty}', 'invalid character property name', offset: 3, length: 15)
      assert_parse_error('\p{^InvalidProperty}', 'invalid character property name', offset: 4, length: 15)
    end

    # ========================================================================
    #
    # Special nodes: grapheme cluster, keep, newline
    #
    # ========================================================================

    def test_grapheme_cluster
      result = parse('\X')
      assert_equal :grapheme_cluster, result[:type]
    end

    def test_keep
      result = parse('\K')
      assert_equal :keep, result[:type]
    end

    def test_newline
      result = parse('\R')
      assert_equal :newline, result[:type]
    end

    # ========================================================================
    #
    # Quantifiers:
    #
    # ========================================================================

    # Greedy quantifiers:

    def test_quantifier_star_greedy
      result = parse('a*')
      assert_equal :quantifier, result[:type]
      assert_equal 0, result[:min]
      assert_equal UINT32_MAX, result[:max]
      assert_equal :greedy, result[:quantifier_type]
      assert_equal :literal, result[:child][:type]
      assert_equal 'a', result[:child][:buf]
    end

    def test_quantifier_plus_greedy
      result = parse('a+')
      assert_equal :quantifier, result[:type]
      assert_equal 1, result[:min]
      assert_equal UINT32_MAX, result[:max]
      assert_equal :greedy, result[:quantifier_type]
    end

    def test_quantifier_question_greedy
      result = parse('a?')
      assert_equal :quantifier, result[:type]
      assert_equal 0, result[:min]
      assert_equal 1, result[:max]
      assert_equal :greedy, result[:quantifier_type]
    end

    # Reluctant quantifiers:

    def test_quantifier_star_reluctant
      result = parse('a*?')
      assert_equal :quantifier, result[:type]
      assert_equal 0, result[:min]
      assert_equal UINT32_MAX, result[:max]
      assert_equal :reluctant, result[:quantifier_type]
    end

    def test_quantifier_plus_reluctant
      result = parse('a+?')
      assert_equal :quantifier, result[:type]
      assert_equal 1, result[:min]
      assert_equal UINT32_MAX, result[:max]
      assert_equal :reluctant, result[:quantifier_type]
    end

    def test_quantifier_question_reluctant
      result = parse('a??')
      assert_equal :quantifier, result[:type]
      assert_equal 0, result[:min]
      assert_equal 1, result[:max]
      assert_equal :reluctant, result[:quantifier_type]
    end

    # Possessive quantifiers:

    def test_quantifier_star_possessive
      result = parse('a*+')
      assert_equal :quantifier, result[:type]
      assert_equal 0, result[:min]
      assert_equal UINT32_MAX, result[:max]
      assert_equal :possessive, result[:quantifier_type]
    end

    def test_quantifier_plus_possessive
      result = parse('a++')
      assert_equal :quantifier, result[:type]
      assert_equal 1, result[:min]
      assert_equal UINT32_MAX, result[:max]
      assert_equal :possessive, result[:quantifier_type]
    end

    def test_quantifier_question_possessive
      result = parse('a?+')
      assert_equal :quantifier, result[:type]
      assert_equal 0, result[:min]
      assert_equal 1, result[:max]
      assert_equal :possessive, result[:quantifier_type]
    end

    # Range quantifiers:

    def test_quantifier_range_exact
      result = parse('a{3}')
      assert_equal :quantifier, result[:type]
      assert_equal 3, result[:min]
      assert_equal 3, result[:max]
      assert_equal :greedy, result[:quantifier_type]
    end

    def test_quantifier_range_min_max
      result = parse('a{2,5}')
      assert_equal :quantifier, result[:type]
      assert_equal 2, result[:min]
      assert_equal 5, result[:max]
      assert_equal :greedy, result[:quantifier_type]
    end

    def test_quantifier_range_min_only
      result = parse('a{2,}')
      assert_equal :quantifier, result[:type]
      assert_equal 2, result[:min]
      assert_equal UINT32_MAX, result[:max]
      assert_equal :greedy, result[:quantifier_type]
    end

    def test_quantifier_range_max_only
      result = parse('a{,5}')
      assert_equal :quantifier, result[:type]
      assert_equal 0, result[:min]
      assert_equal 5, result[:max]
      assert_equal :greedy, result[:quantifier_type]
    end

    def test_quantifier_range_reluctant
      result = parse('a{2,5}?')
      assert_equal :quantifier, result[:type]
      assert_equal 2, result[:min]
      assert_equal 5, result[:max]
      assert_equal :reluctant, result[:quantifier_type]
    end

    def test_quantifier_range_possessive
      result = parse('a{2,5}+')
      assert_equal :quantifier, result[:type]
      assert_equal 2, result[:min]
      assert_equal 5, result[:max]
      assert_equal :possessive, result[:quantifier_type]
    end

    def test_quantifier_range_exact_not_reluctant
      # `{n}` does not allow reluctant modifier, so `?` becomes a separate quantifier.
      result = parse('a{3}?')
      assert_equal :quantifier, result[:type]
      assert_equal 0, result[:min]
      assert_equal 1, result[:max]
      child = result[:child]
      assert_equal :quantifier, child[:type]
      assert_equal 3, child[:min]
      assert_equal 3, child[:max]
    end

    # Incomplete range quantifiers (treated as literals):

    def test_incomplete_range_quantifier_brace_only
      result = parse('a{')
      assert_equal :literal, result[:type]
      assert_equal 'a{', result[:buf]
    end

    def test_incomplete_range_quantifier_no_closing_brace
      result = parse('a{3')
      assert_equal :literal, result[:type]
      assert_equal 'a{3', result[:buf]
    end

    def test_incomplete_range_quantifier_comma_no_close
      result = parse('a{2,5')
      assert_equal :literal, result[:type]
      assert_equal 'a{2,5', result[:buf]
    end

    # Quantifier on non-literal:

    def test_quantifier_on_dot
      result = parse('.*')
      assert_equal :quantifier, result[:type]
      assert_equal :dot, result[:child][:type]
    end

    def test_quantifier_on_char_type
      result = parse('\d+')
      assert_equal :quantifier, result[:type]
      assert_equal :char_type, result[:child][:type]
      assert_equal :digit, result[:child][:char_type]
    end

    def test_quantifier_nothing_to_repeat
      assert_parse_error('*', 'nothing to repeat', offset: 0, length: 1)
      assert_parse_error('+', 'nothing to repeat', offset: 0, length: 1)
      assert_parse_error('?', 'nothing to repeat', offset: 0, length: 1)
      assert_parse_error('{1}', 'nothing to repeat', offset: 0, length: 3)
    end

    def test_quantifier_error_too_large_number
      assert_parse_error('a{1000001}', 'number in quantifier is too large', offset: 2, length: 7)
    end

    def test_quantifier_error_numbers_out_of_order
      assert_parse_error('a{2,1}', 'numbers in quantifier are out of order', offset: 5, length: 0)
    end

    # ========================================================================
    #
    # Concatenation:
    #
    # ========================================================================

    def test_concat_two_elements
      result = parse('a.')
      assert_equal :concat, result[:type]
      assert_equal 2, result[:children].length
      assert_equal :literal, result[:children][0][:type]
      assert_equal 'a', result[:children][0][:buf]
      assert_equal :dot, result[:children][1][:type]
    end

    def test_concat_three_elements
      result = parse('a.b')
      assert_equal :concat, result[:type]
      assert_equal 3, result[:children].length
      assert_equal :literal, result[:children][0][:type]
      assert_equal 'a', result[:children][0][:buf]
      assert_equal :dot, result[:children][1][:type]
      assert_equal :literal, result[:children][2][:type]
      assert_equal 'b', result[:children][2][:buf]
    end

    def test_concat_literal_merging
      result = parse('ab.cd')
      assert_equal :concat, result[:type]
      assert_equal 3, result[:children].length
      assert_equal :literal, result[:children][0][:type]
      assert_equal 'ab', result[:children][0][:buf]
      assert_equal :dot, result[:children][1][:type]
      assert_equal :literal, result[:children][2][:type]
      assert_equal 'cd', result[:children][2][:buf]
    end

    def test_concat_with_quantifier
      result = parse('ab*c')
      assert_equal :concat, result[:type]
      assert_equal 3, result[:children].length
      assert_equal :literal, result[:children][0][:type]
      assert_equal 'a', result[:children][0][:buf]
      assert_equal :quantifier, result[:children][1][:type]
      assert_equal :literal, result[:children][2][:type]
      assert_equal 'c', result[:children][2][:buf]
    end

    # ========================================================================
    #
    # Alternation:
    #
    # ========================================================================

    def test_alt_two_branches
      result = parse('a|b')
      assert_equal :alt, result[:type]
      assert_equal 2, result[:children].length
      assert_equal :literal, result[:children][0][:type]
      assert_equal 'a', result[:children][0][:buf]
      assert_equal :literal, result[:children][1][:type]
      assert_equal 'b', result[:children][1][:buf]
    end

    def test_alt_three_branches
      result = parse('a|b|c')
      assert_equal :alt, result[:type]
      assert_equal 3, result[:children].length
      assert_equal :literal, result[:children][0][:type]
      assert_equal :literal, result[:children][1][:type]
      assert_equal :literal, result[:children][2][:type]
    end

    def test_alt_with_empty_left
      result = parse('|a')
      assert_equal :alt, result[:type]
      assert_equal 2, result[:children].length
      assert_equal :concat, result[:children][0][:type]
      assert_equal [], result[:children][0][:children]
      assert_equal :literal, result[:children][1][:type]
    end

    def test_alt_with_empty_right
      result = parse('a|')
      assert_equal :alt, result[:type]
      assert_equal 2, result[:children].length
      assert_equal :literal, result[:children][0][:type]
      assert_equal :concat, result[:children][1][:type]
      assert_equal [], result[:children][1][:children]
    end

    def test_alt_both_empty
      result = parse('|')
      assert_equal :alt, result[:type]
      assert_equal 2, result[:children].length
      assert_equal :concat, result[:children][0][:type]
      assert_equal :concat, result[:children][1][:type]
    end

    def test_alt_with_concat
      result = parse('ab|cd')
      assert_equal :alt, result[:type]
      assert_equal 2, result[:children].length
      assert_equal :literal, result[:children][0][:type]
      assert_equal 'ab', result[:children][0][:buf]
      assert_equal :literal, result[:children][1][:type]
      assert_equal 'cd', result[:children][1][:buf]
    end

    def test_node_span_for_alt
      result = parse('a|bc')
      assert_equal :alt, result[:type]
      assert_equal 0, result[:span_offset]
      assert_equal 4, result[:span_length]

      left = result[:children][0]
      assert_equal 0, left[:span_offset]
      assert_equal 1, left[:span_length]

      right = result[:children][1]
      assert_equal 2, right[:span_offset]
      assert_equal 2, right[:span_length]
    end

    # ========================================================================
    #
    # Extended mode:
    #
    # ========================================================================

    def test_extended_mode_ignores_spaces
      result = parse('a b', is_extended_mode: true)
      assert_equal :literal, result[:type]
      assert_equal 'ab', result[:buf]
    end

    def test_extended_mode_ignores_tabs
      result = parse("a\tb", is_extended_mode: true)
      assert_equal :literal, result[:type]
      assert_equal 'ab', result[:buf]
    end

    def test_extended_mode_ignores_newlines
      result = parse("a\nb", is_extended_mode: true)
      assert_equal :literal, result[:type]
      assert_equal 'ab', result[:buf]
    end

    def test_extended_mode_ignores_comments
      result = parse("a# comment\nb", is_extended_mode: true)
      assert_equal :literal, result[:type]
      assert_equal 'ab', result[:buf]
    end

    def test_non_extended_mode_preserves_spaces
      result = parse('a b')
      assert_equal :literal, result[:type]
      assert_equal 'a b', result[:buf]
    end

    def test_non_extended_mode_hash_is_literal
      result = parse('a#b')
      assert_equal :literal, result[:type]
      assert_equal 'a#b', result[:buf]
    end

    # ========================================================================
    #
    # Parser options:
    #
    # ========================================================================

    def test_ignore_case_flag_on_literal
      result = parse('a', is_ignore_case: true)
      assert_equal :literal, result[:type]
      assert_equal true, result[:is_ignore_case]
    end

    def test_ignore_case_flag_off_on_literal
      result = parse('a')
      assert_equal :literal, result[:type]
      assert_equal false, result[:is_ignore_case]
    end

    def test_dot_allows_newline_flag
      result = parse('.', dot_allows_newline: true)
      assert_equal :dot, result[:type]
      assert_equal true, result[:allows_newline]
    end

    def test_dot_disallows_newline_flag
      result = parse('.')
      assert_equal :dot, result[:type]
      assert_equal false, result[:allows_newline]
    end

    # ========================================================================
    #
    # Combined patterns:
    #
    # ========================================================================

    def test_combined_literal_quantifier_alt
      # `ab*|cd+` should parse as alt(concat(a, b*), concat(c, d+))
      result = parse('ab*|cd+')
      assert_equal :alt, result[:type]
      assert_equal 2, result[:children].length

      left = result[:children][0]
      assert_equal :concat, left[:type]
      assert_equal 2, left[:children].length
      assert_equal :literal, left[:children][0][:type]
      assert_equal 'a', left[:children][0][:buf]
      assert_equal :quantifier, left[:children][1][:type]

      right = result[:children][1]
      assert_equal :concat, right[:type]
      assert_equal 2, right[:children].length
      assert_equal :literal, right[:children][0][:type]
      assert_equal 'c', right[:children][0][:buf]
      assert_equal :quantifier, right[:children][1][:type]
    end

    def test_multiple_quantifiers
      result = parse('a*{2}')
      assert_equal :quantifier, result[:type]
      assert_equal 2, result[:min]
      assert_equal 2, result[:max]
      assert_equal :greedy, result[:quantifier_type]

      child = result[:child]
      assert_equal :quantifier, child[:type]
      assert_equal 0, child[:min]
      assert_equal UINT32_MAX, child[:max]
      assert_equal :greedy, child[:quantifier_type]

      child = child[:child]
      assert_equal :literal, child[:type]
      assert_equal 'a', child[:buf]
    end

    def test_assertion_in_concat
      result = parse('^a$')
      assert_equal :concat, result[:type]
      assert_equal 3, result[:children].length
      assert_equal :assertion, result[:children][0][:type]
      assert_equal :begin_of_line, result[:children][0][:assertion_type]
      assert_equal :literal, result[:children][1][:type]
      assert_equal :assertion, result[:children][2][:type]
      assert_equal :end_of_line, result[:children][2][:assertion_type]
    end

    # ========================================================================
    #
    # Groups:
    #
    # ========================================================================

    def test_group_capturing
      result = parse('(a)')
      assert_equal :group, result[:type]
      assert_equal 1, result[:group_num]
      assert_equal :literal, result[:child][:type]
      assert_equal 'a', result[:child][:buf]
    end

    def test_group_nested
      result = parse('(a(b))')
      assert_equal :group, result[:type]
      assert_equal 1, result[:group_num]

      child = result[:child]
      assert_equal :concat, child[:type]
      assert_equal 2, child[:children].length
      assert_equal :literal, child[:children][0][:type]
      assert_equal 'a', child[:children][0][:buf]

      inner_group = child[:children][1]
      assert_equal :group, inner_group[:type]
      assert_equal 2, inner_group[:group_num]
      assert_equal 'b', inner_group[:child][:buf]
    end

    def test_group_non_capturing
      result = parse('(?:a)')
      assert_equal :group, result[:type]
      assert_equal false, result[:has_name]
      assert_equal 0, result[:group_num]
      assert_equal :literal, result[:child][:type]
      assert_equal 'a', result[:child][:buf]
    end

    def test_group_inline_options
      result = parse('(?i:a)a')
      assert_equal :concat, result[:type]
      assert_equal 2, result[:children].length

      group = result[:children][0]
      assert_equal :group, group[:type]
      assert_equal :literal, group[:child][:type]
      assert_equal true, group[:child][:is_ignore_case]

      literal = result[:children][1]
      assert_equal :literal, literal[:type]
      assert_equal 'a', literal[:buf]
      assert_equal false, literal[:is_ignore_case]
    end

    def test_undefined_group_option
      assert_parse_error('(?z)', 'undefined group option', offset: 2, length: 0)
    end

    def test_group_named
      result = parse('(?<name>a)')
      assert_equal :group, result[:type]
      assert_equal true, result[:has_name]
      assert_equal 'name', result[:name]
      assert_equal 0, result[:group_num]

      result = parse("(?'name'a)")
      assert_equal :group, result[:type]
      assert_equal true, result[:has_name]
      assert_equal 'name', result[:name]
      assert_equal 0, result[:group_num]
    end

    def test_error_invalid_group_name
      assert_parse_error('(?<-a>)', 'invalid group name', offset: 4, length: 0)
    end

    def test_error_empty_group_name
      assert_parse_error('(?<>)', 'empty group name', offset: 3, length: 0)
      assert_parse_error("(?'')", 'empty group name', offset: 3, length: 0)
    end

    def test_lookahead_positive
      result = parse('(?=a)')
      assert_equal :assertion, result[:type]
      assert_equal :positive_lookahead, result[:assertion_type]
      assert_equal :literal, result[:child][:type]
      assert_equal 'a', result[:child][:buf]
    end

    def test_lookahead_negative
      result = parse('(?!a)')
      assert_equal :assertion, result[:type]
      assert_equal :negative_lookahead, result[:assertion_type]
      assert_equal :literal, result[:child][:type]
      assert_equal 'a', result[:child][:buf]
    end

    def test_lookbehind_positive
      result = parse('(?<=a)')
      assert_equal :assertion, result[:type]
      assert_equal :positive_lookbehind, result[:assertion_type]
      assert_equal :literal, result[:child][:type]
      assert_equal 'a', result[:child][:buf]
    end

    def test_lookbehind_negative
      result = parse('(?<!a)')
      assert_equal :assertion, result[:type]
      assert_equal :negative_lookbehind, result[:assertion_type]
      assert_equal :literal, result[:child][:type]
      assert_equal 'a', result[:child][:buf]
    end

    def test_atomic_group
      result = parse('(?>a)')
      assert_equal :atomic, result[:type]
      assert_equal :literal, result[:child][:type]
      assert_equal 'a', result[:child][:buf]
    end

    def test_absence_group
      result = parse('(?~a)')
      assert_equal :absence, result[:type]
      assert_equal :literal, result[:child][:type]
      assert_equal 'a', result[:child][:buf]
    end

    def test_conditional
      result = parse('(?(1)a|b)')
      assert_equal :conditional, result[:type]
      assert_equal 1, result[:group_num]
      assert_equal :literal, result[:yes_child][:type]
      assert_equal 'a', result[:yes_child][:buf]
      assert_equal :literal, result[:no_child][:type]
      assert_equal 'b', result[:no_child][:buf]

      # Conditional with only true branch
      result = parse('(?(1)a)')
      assert_equal :conditional, result[:type]
      assert_equal 1, result[:group_num]
      assert_equal :literal, result[:yes_child][:type]
      assert_equal 'a', result[:yes_child][:buf]
      assert_equal nil, result[:no_child]
    end

    def test_conditional_with_depth
      result = parse('(?(1+1)a|b)')
      assert_equal :conditional, result[:type]
      assert_equal 1, result[:group_num]
      assert_equal true, result[:has_depth]
      assert_equal 1, result[:depth]
      assert_equal :literal, result[:yes_child][:type]
      assert_equal 'a', result[:yes_child][:buf]
      assert_equal :literal, result[:no_child][:type]
      assert_equal 'b', result[:no_child][:buf]
    end

    def test_conditional_named
      result = parse('(?(<name>)a|b)')
      assert_equal :conditional, result[:type]
      assert_equal true, result[:has_name]
      assert_equal 'name', result[:name]
      assert_equal :literal, result[:yes_child][:type]
      assert_equal 'a', result[:yes_child][:buf]
      assert_equal :literal, result[:no_child][:type]
      assert_equal 'b', result[:no_child][:buf]

      result = parse(%q{(?('name')a|b)})
      assert_equal :conditional, result[:type]
      assert_equal true, result[:has_name]
      assert_equal 'name', result[:name]
      assert_equal :literal, result[:yes_child][:type]
      assert_equal 'a', result[:yes_child][:buf]
      assert_equal :literal, result[:no_child][:type]
      assert_equal 'b', result[:no_child][:buf]
    end

    def test_conditional_incomplete
      assert_parse_error('(?(1', 'incomplete group specifier', offset: 4, length: 0)
    end

    def test_conditional_invalid_group_number
      assert_parse_error('(?(0)a|b)', 'invalid conditional group number', offset: 3, length: 1)
    end

    def test_conditional_errors
      assert_parse_error('(?(<>)a|b)', 'empty group name', offset: 4, length: 0)
      assert_parse_error('(?(x)a|b)', 'incomplete group specifier', offset: 3, length: 0)
      assert_parse_error('(?(<name)a)', 'incomplete group specifier', offset: 11, length: 0)
      assert_parse_error('(?(1)a', 'invalid conditional group', offset: 6, length: 0)
      assert_parse_error('(?(1)a|b', 'invalid conditional group', offset: 8, length: 0)
    end

    def test_unclosed_group
      assert_parse_error('(', 'unterminated group: missing closing parenthesis', offset: 1, length: 0)
      assert_parse_error('(abc', 'unterminated group: missing closing parenthesis', offset: 4, length: 0)
    end

    def test_unmatched_close_paren
      assert_parse_error(')', 'unmatched close parenthesis', offset: 0, length: 1)
      assert_parse_error('abc)', 'unmatched close parenthesis', offset: 3, length: 1)
    end

    def test_incomplete_group_specifier
      assert_parse_error('(?', 'incomplete group specifier', offset: 2, length: 0)
    end

    # ========================================================================
    #
    # Character classes:
    #
    # ========================================================================

    def test_char_class
      result = parse('[abc]')
      assert_equal :char_class, result[:type]
      assert_equal true, result[:is_positive]
      assert_equal 1, result[:unions].length
      assert_equal 3, result[:unions][0][:items].length
      assert_equal :code, result[:unions][0][:items][0][:type]
      assert_equal 97, result[:unions][0][:items][0][:code]
      assert_equal :code, result[:unions][0][:items][1][:type]
      assert_equal 98, result[:unions][0][:items][1][:code]
      assert_equal :code, result[:unions][0][:items][2][:type]
      assert_equal 99, result[:unions][0][:items][2][:code]

      result = parse('[\\d\\w]')
      assert_equal :char_class, result[:type]
      assert_equal :char_type, result[:unions][0][:items][0][:type]
      assert_equal true, result[:unions][0][:items][0][:is_positive]
      assert_equal :digit, result[:unions][0][:items][0][:char_type]
      assert_equal :char_type, result[:unions][0][:items][1][:type]
      assert_equal true, result[:unions][0][:items][1][:is_positive]
      assert_equal :word, result[:unions][0][:items][1][:char_type]

      result = parse('[\\p{Lu}]')
      assert_equal :char_class, result[:type]
      assert_equal :char_prop, result[:unions][0][:items][0][:type]
      assert_equal true, result[:unions][0][:items][0][:is_positive]
      assert_equal Naraku::Encoding.name_to_cprop('Lu'), result[:unions][0][:items][0][:cprop]
    end

    def test_char_class_negated
      result = parse('[^abc]')
      assert_equal :char_class, result[:type]
      assert_equal false, result[:is_positive]
      assert_equal 3, result[:unions][0][:items].length
      assert_equal :code, result[:unions][0][:items][0][:type]
      assert_equal 97, result[:unions][0][:items][0][:code]
    end

    def test_char_class_range
      result = parse('[a-z]')
      assert_equal :char_class, result[:type]
      assert_equal :range, result[:unions][0][:items][0][:type]
      assert_equal 97, result[:unions][0][:items][0][:begin_code]
      assert_equal 122, result[:unions][0][:items][0][:end_code]

      result = parse('[a-z0-9]')
      assert_equal :char_class, result[:type]
      assert_equal :range, result[:unions][0][:items][0][:type]
      assert_equal :range, result[:unions][0][:items][1][:type]

      result = parse('[-a]')
      assert_equal :char_class, result[:type]
      assert_equal :code, result[:unions][0][:items][0][:type]
      assert_equal 45, result[:unions][0][:items][0][:code]

      result = parse('[a-]')
      assert_equal :char_class, result[:type]
      assert_equal :code, result[:unions][0][:items][1][:type]
      assert_equal 45, result[:unions][0][:items][1][:code]
    end

    def test_char_class_nested
      result = parse('[[ab]]')
      assert_equal :char_class, result[:type]
      assert_equal :nested_char_class, result[:unions][0][:items][0][:type]
      assert_equal true, result[:unions][0][:items][0][:is_positive]
      assert_equal :code, result[:unions][0][:items][0][:unions][0][:items][0][:type]
      assert_equal 97, result[:unions][0][:items][0][:unions][0][:items][0][:code]

      result = parse('[[^a]&&[ab]]')
      assert_equal :char_class, result[:type]
      assert_equal 2, result[:unions].length
      assert_equal :nested_char_class, result[:unions][0][:items][0][:type]
      assert_equal false, result[:unions][0][:items][0][:is_positive]
    end

    def test_char_class_intersection
      result = parse('[a&&b]')
      assert_equal :char_class, result[:type]
      assert_equal 2, result[:unions].length
      assert_equal :code, result[:unions][0][:items][0][:type]
      assert_equal 97, result[:unions][0][:items][0][:code]
      assert_equal :code, result[:unions][1][:items][0][:type]
      assert_equal 98, result[:unions][1][:items][0][:code]

      result = parse('[a&&b&&c]')
      assert_equal :char_class, result[:type]
      assert_equal 3, result[:unions].length
      assert_equal :code, result[:unions][2][:items][0][:type]
      assert_equal 99, result[:unions][2][:items][0][:code]
    end

    def test_char_class_posix
      result = parse('[[:digit:]]')
      assert_equal :char_class, result[:type]
      assert_equal :posix_char_class, result[:unions][0][:items][0][:type]
      assert_equal true, result[:unions][0][:items][0][:is_positive]
      assert_equal :digit, result[:unions][0][:items][0][:posix_char_class]

      result = parse('[[:^digit:]]')
      assert_equal :char_class, result[:type]
      assert_equal :posix_char_class, result[:unions][0][:items][0][:type]
      assert_equal false, result[:unions][0][:items][0][:is_positive]
      assert_equal :digit, result[:unions][0][:items][0][:posix_char_class]
    end

    def test_char_class_errors
      assert_parse_error('[', 'unterminated character class', offset: 1, length: 0)
      assert_parse_error('[]', 'empty character class', offset: 0, length: 2)
      assert_parse_error('[z-a]', 'character class range out of order', offset: 1, length: 3)
      assert_parse_error('[\\d-\\w]', 'invalid character class range', offset: 1, length: 2)
      assert_parse_error('[a-\\d]', 'invalid character class range', offset: 3, length: 2)
      assert_parse_error('[a-\\p{Lu}]', 'invalid character class range', offset: 3, length: 6)
      assert_parse_error('[[:^:]]', 'empty POSIX character class name', offset: 4, length: 0)
      assert_parse_error('[[:foo:]]', 'invalid POSIX character class name', offset: 3, length: 3)
      assert_parse_error('[[:^foo:]]', 'invalid POSIX character class name', offset: 4, length: 3)
      assert_parse_error('[[:digitx:]]', 'invalid POSIX character class name', offset: 3, length: 6)
    end

    def test_char_class_span
      result = parse('[a-z]')
      assert_equal :char_class, result[:type]
      assert_equal 0, result[:span_offset]
      assert_equal 5, result[:span_length]

      union = result[:unions][0]
      assert_equal 1, union[:span_offset]
      assert_equal 3, union[:span_length]

      item = union[:items][0]
      assert_equal :range, item[:type]
      assert_equal 1, item[:span_offset]
      assert_equal 3, item[:span_length]
    end

    # ========================================================================
    #
    # Back references:
    #
    # ========================================================================

    def test_back_ref_number
      result = parse('\1')
      assert_equal :back_ref, result[:type]
      assert_equal false, result[:has_name]
      assert_equal 1, result[:group_num]

      result = parse('\k<1>')
      assert_equal :back_ref, result[:type]
      assert_equal false, result[:has_name]
      assert_equal 1, result[:group_num]

      # Relative back-reference
      result = parse('()\k<-1>')
      assert_equal :concat, result[:type]
      assert_equal 2, result[:children].length
      assert_equal :back_ref, result[:children][1][:type]
      assert_equal 1, result[:children][1][:group_num]
    end

    def test_back_ref_named
      result = parse('\k<name>')
      assert_equal :back_ref, result[:type]
      assert_equal true, result[:has_name]
      assert_equal 'name', result[:name]

      result = parse(%q{\k'name'})
      assert_equal :back_ref, result[:type]
      assert_equal true, result[:has_name]
      assert_equal 'name', result[:name]

      # With depth
      result = parse('\k<name+1>')
      assert_equal :back_ref, result[:type]
      assert_equal true, result[:has_name]
      assert_equal 'name', result[:name]
      assert_equal true, result[:has_depth]
      assert_equal 1, result[:depth]

      result = parse('\k<name-1>')
      assert_equal :back_ref, result[:type]
      assert_equal true, result[:has_name]
      assert_equal 'name', result[:name]
      assert_equal true, result[:has_depth]
      assert_equal(-1, result[:depth])
    end

    def test_back_ref_literal
      result = parse('\k')
      assert_equal :literal, result[:type]
      assert_equal 'k', result[:buf]

      result = parse('\ka')
      assert_equal :literal, result[:type]
      assert_equal 'ka', result[:buf]
    end

    def test_error_group_number_out_of_range
      # In \k<-1>, the error is reported at > (offset 5)
      assert_parse_error('\k<-1>', 'group number is out of range', offset: 5, length: 0)
      assert_parse_error('\g<-1>', 'group number is out of range', offset: 5, length: 0)
    end

    def test_error_incomplete_back_ref
      assert_parse_error('\k<', 'incomplete back reference', offset: 3, length: 0)
      assert_parse_error('\k<name', 'incomplete back reference', offset: 7, length: 0)
    end

    def test_error_incomplete_capture_depth
      assert_parse_error('\k<name+', 'incomplete capture depth', offset: 8, length: 0)
      assert_parse_error('\k<name-', 'incomplete capture depth', offset: 8, length: 0)
    end

    def test_error_capture_depth_too_large
      assert_parse_error('\k<name+1001>', 'capture depth is too large', offset: 8, length: 4)
    end

    def test_error_group_number_too_large
      assert_parse_error('\k<10000001>', 'group number is too large', offset: 3, length: 8)
      assert_parse_error('\g<10000001>', 'group number is too large', offset: 3, length: 8)
    end

    def test_error_invalid_back_ref
      assert_parse_error('\k<0>', 'invalid back reference', offset: 3, length: 1)
    end

    # ========================================================================
    #
    # Sub-expression calls:
    #
    # ========================================================================

    def test_subexp_call
      result = parse('\g<1>')
      assert_equal :call, result[:type]
      assert_equal false, result[:has_name]
      assert_equal 1, result[:group_num]

      result = parse('\g<0>')
      assert_equal :call, result[:type]
      assert_equal false, result[:has_name]
      assert_equal 0, result[:group_num]

      result = parse('\g<name>')
      assert_equal :call, result[:type]
      assert_equal true, result[:has_name]
      assert_equal 'name', result[:name]

      result = parse(%q{\g'name'})
      assert_equal :call, result[:type]
      assert_equal true, result[:has_name]
      assert_equal 'name', result[:name]

      # Relative call
      result = parse('()\g<-1>')
      assert_equal :concat, result[:type]
      assert_equal 2, result[:children].length
      assert_equal :call, result[:children][1][:type]
      assert_equal 1, result[:children][1][:group_num]
    end

    def test_subexp_call_literal
      result = parse('\g')
      assert_equal :literal, result[:type]
      assert_equal 'g', result[:buf]

      result = parse('\ga')
      assert_equal :literal, result[:type]
      assert_equal 'ga', result[:buf]
    end

    def test_error_incomplete_subexp_call
      assert_parse_error('\g<', 'incomplete sub-expression call', offset: 3, length: 0)
      assert_parse_error('\g<name', 'incomplete sub-expression call', offset: 7, length: 0)
    end
  end
end
