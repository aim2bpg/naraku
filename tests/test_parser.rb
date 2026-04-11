module Parser
  UINT32_MAX = (2**32) - 1

  class TestParser < Mtest::Test
    def parse(pattern, encoding: Naraku::Encoding::UTF_8, **options)
      parser = Naraku::Parser.new(encoding, pattern, **options)
      node = parser.parse
      node.to_h
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
      assert_raises(Naraku::ParseError, 'code point is out of range (at offset 0)') { parse('\u{110000}') }
    end

    def test_unicode_escape_surrogate
      # Surrogate code points are invalid in UTF-8.
      assert_raises(Naraku::ParseError, 'invalid code point (at offset 0)') { parse('\u{D800}') }
      assert_raises(Naraku::ParseError, 'invalid code point (at offset 0)') { parse('\u{DFFF}') }
    end

    def test_unicode_escape_encoding_constraint
      # U+0080 is out of range for US-ASCII.
      assert_raises(Naraku::ParseError, 'Unicode escape sequence in non-Unicode encoding (at offset 2)') { parse('\u0080', encoding: Naraku::Encoding::US_ASCII) }
      assert_raises(Naraku::ParseError, 'Unicode escape sequence in non-Unicode encoding (at offset 2)') { parse('\u{80}', encoding: Naraku::Encoding::US_ASCII) }
    end

    def test_unicode_escape_errors
      # Trailing \u
      assert_raises(Naraku::ParseError, 'unclosed Unicode escape sequence brace (at offset 2)') { parse('\u') }
      # Too short fixed escape
      assert_raises(Naraku::ParseError, 'incomplete Unicode escape sequence (at offset 5)') { parse('\u123') }
      # Invalid hex digit
      assert_raises(Naraku::ParseError, 'incomplete Unicode escape sequence (at offset 5)') { parse('\u123G') }
      # Unclosed brace
      assert_raises(Naraku::ParseError, 'unclosed Unicode escape sequence brace (at offset 5)') { parse('\u{61') }
      # Invalid hex in brace
      assert_raises(Naraku::ParseError, 'invalid Unicode escape sequence (at offset 3)') { parse('\u{G}') }
      # Empty brace
      assert_raises(Naraku::ParseError, 'empty Unicode escape sequence brace (at offset 3)') { parse('\u{}') }
    end

    # ========================================================================
    #
    # Escape sequences:
    #
    # ========================================================================

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
      assert_raises(Naraku::ParseError, 'incomplete \x escape sequence (at offset 2)') { parse('\x') }
      # Invalid hex digit
      assert_raises(Naraku::ParseError, 'incomplete \x escape sequence (at offset 2)') { parse('\xG') }
      # Missing meta character
      assert_raises(Naraku::ParseError, 'incomplete \M- escape sequence (at offset 2)') { parse('\M') }
      assert_raises(Naraku::ParseError, 'incomplete \M- escape sequence (at offset 3)') { parse('\M-') }
      # Duplicate prefixes
      assert_raises(Naraku::ParseError, 'duplicate \M- escape sequence (at offset 5)') { parse('\M-\M-a') }
      assert_raises(Naraku::ParseError, 'duplicate \c/\C- escape sequence (at offset 5)') { parse('\C-\C-a') }
      # Invalid control/meta character
      assert_raises(Naraku::ParseError, 'invalid code in \c/\C- escape sequence (at offset 6)') { parse('\C-あ') }
      assert_raises(Naraku::ParseError, 'invalid code in \c/\C- escape sequence (at offset 5)') { parse('\cあ') }
      assert_raises(Naraku::ParseError, 'invalid code in \M- escape sequence (at offset 6)') { parse('\M-あ') }
      # Incomplete multibyte sequence (only first byte of 'あ')
      assert_raises(Naraku::ParseError, 'incomplete escaped byte sequence (at offset 4)') { parse('\xe3') }
      assert_raises(Naraku::ParseError, 'incomplete escaped byte sequence (at offset 8)') { parse('\xe3\x81') }
      # Invalid multibyte sequence (surrogate code point)
      assert_raises(Naraku::ParseError, 'invalid escaped byte sequence (at offset 12)') { parse('\xED\xA0\x80') }
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
      assert_raises(Naraku::ParseError, 'unclosed character property escape sequence brace (at offset 5)') { parse('\p{Lu') }
      # Invalid property name
      assert_raises(Naraku::ParseError, 'invalid character property name (at offset 3)') { parse('\p{InvalidProperty}') }
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
    # Error cases:
    #
    # ========================================================================

    def test_error_trailing_backslash
      assert_raises(Naraku::ParseError, 'incomplete escape sequence (at offset 1)') { parse('\\') }
    end

    # ========================================================================
    #
    # Stubs for unimplemented features:
    #
    # ========================================================================

    def test_char_class
      skip 'character classes are not yet implemented'
    end

    def test_char_class_negated
      skip 'character classes are not yet implemented'
    end

    def test_char_class_range
      skip 'character classes are not yet implemented'
    end

    def test_char_class_nested
      skip 'character classes are not yet implemented'
    end

    def test_char_class_intersection
      skip 'character classes are not yet implemented'
    end

    def test_char_class_posix
      skip 'character classes are not yet implemented'
    end

    def test_group_capturing
      skip 'groups are not yet implemented'
    end

    def test_group_non_capturing
      skip 'groups are not yet implemented'
    end

    def test_group_named
      skip 'groups are not yet implemented'
    end

    def test_lookahead_positive
      skip 'lookahead is not yet implemented'
    end

    def test_lookahead_negative
      skip 'lookahead is not yet implemented'
    end

    def test_lookbehind_positive
      skip 'lookbehind is not yet implemented'
    end

    def test_lookbehind_negative
      skip 'lookbehind is not yet implemented'
    end

    def test_atomic_group
      skip 'atomic groups are not yet implemented'
    end

    def test_back_ref_number
      skip 'back references are not yet implemented'
    end

    def test_back_ref_named
      skip 'back references are not yet implemented'
    end

    def test_call_subroutine
      skip 'subroutine calls are not yet implemented'
    end

    def test_conditional
      skip 'conditionals are not yet implemented'
    end
  end
end
