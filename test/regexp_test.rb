class RegexpTest < Mtest::Test
  def match(pattern, subject, byte_start: 0, **parser_options)
    Naraku::Regexp.new(pattern, **parser_options).match(subject, byte_start)
  end

  def assert_compile_error(pattern, error_substr, **parser_options)
    begin
      Naraku::Regexp.new(pattern, **parser_options)
    rescue Naraku::CompileError => e
      assert e.message.include?(error_substr), "expected #{e.message.inspect} to include #{error_substr.inspect}"
      return
    end
    assert false, "expected Naraku::CompileError for #{pattern.inspect}"
  end

  # ========================================================================
  # Basic match / no-match
  # ========================================================================

  def test_literal_match
    md = match('abc', 'xabcy')
    assert !md.nil?
    assert_equal 'abc', md[0]
    assert_equal 'x', md.pre_match
    assert_equal 'y', md.post_match
  end

  def test_literal_no_match
    md = match('abc', 'xyz')
    assert_nil md
  end

  def test_empty_pattern_matches_at_start
    md = match('', 'abc')
    assert !md.nil?
    assert_equal '', md[0]
    assert_equal 0, md.byte_begin(0)
    assert_equal 0, md.byte_end(0)
  end

  # ========================================================================
  # Alternation and concatenation
  # ========================================================================

  def test_alternation
    md = match('ab|cd', 'xxcdyy')
    assert !md.nil?
    assert_equal 'cd', md[0]
  end

  def test_leftmost_match
    md = match('ab|cd', 'xabcd')
    assert !md.nil?
    assert_equal 'ab', md[0]
  end

  # ========================================================================
  # Quantifiers
  # ========================================================================

  def test_greedy_star
    md = match('a*', 'aaab')
    assert !md.nil?
    assert_equal 'aaa', md[0]
  end

  def test_greedy_plus
    md = match('a+b', 'aaab')
    assert !md.nil?
    assert_equal 'aaab', md[0]
  end

  def test_greedy_question
    md = match('ab?c', 'ac')
    assert !md.nil?
    assert_equal 'ac', md[0]
  end

  def test_lazy_plus
    md = match('a+?b', 'aaab')
    assert !md.nil?
    assert_equal 'aaab', md[0]
  end

  def test_counted_quantifier
    md = match('a{2,4}b+', 'aaabbb')
    assert !md.nil?
    assert_equal 'aaabbb', md[0]
  end

  def test_zero_or_more_no_match_subject
    md = match('a+', 'bbb')
    assert_nil md
  end

  # ========================================================================
  # Capture groups
  # ========================================================================

  def test_single_capture
    md = match('a(b|c)+d', 'xabcbd')
    assert !md.nil?
    assert_equal 'abcbd', md[0]
    assert_equal 'b', md[1]
    assert_equal 1, md.byte_begin(0)
    assert_equal 6, md.byte_end(0)
  end

  def test_captures_array
    md = match('(a)(b)(c)', 'xabcy')
    assert !md.nil?
    assert_equal %w[abc a b c], md.to_a
    assert_equal %w[a b c], md.captures
  end

  def test_unmatched_optional_capture_is_nil
    md = match('(a)?(b)', 'b')
    assert !md.nil?
    assert_equal 'b', md[0]
    assert_nil md[1]
    assert_equal 'b', md[2]
  end

  def test_nested_captures
    md = match('((a)(b))', 'ab')
    assert !md.nil?
    assert_equal 'ab', md[0]
    assert_equal 'ab', md[1]
    assert_equal 'a', md[2]
    assert_equal 'b', md[3]
  end

  # ========================================================================
  # Character classes
  # ========================================================================

  def test_char_class_range
    md = match('[a-z]+', 'ABC')
    assert_nil md

    md = match('[a-z]+', 'ABCabc')
    assert !md.nil?
    assert_equal 'abc', md[0]
  end

  def test_negated_char_class
    md = match('[^abc]+', 'xxxabcyyy')
    assert !md.nil?
    assert_equal 'xxx', md[0]
  end

  def test_char_type_word
    md = match('\\w+', 'abc123 zzz')
    assert !md.nil?
    assert_equal 'abc123', md[0]
  end

  def test_char_type_digit
    md = match('\\d+', 'abc123')
    assert !md.nil?
    assert_equal '123', md[0]
  end

  def test_char_prop
    md = match('\\p{Lu}+', 'xABCd')
    assert !md.nil?
    assert_equal 'ABC', md[0]
  end

  # ========================================================================
  # Dot
  # ========================================================================

  def test_dot_matches_non_newline
    md = match('a.b', 'acb')
    assert !md.nil?
    assert_equal 'acb', md[0]
  end

  def test_dot_does_not_match_newline_by_default
    md = match('a.b', "a\nb")
    assert_nil md
  end

  def test_dot_allows_newline_option
    md = match('a.b', "a\nb", dot_allows_newline: true)
    assert !md.nil?
    assert_equal "a\nb", md[0]
  end

  # ========================================================================
  # Anchors
  # ========================================================================

  def test_begin_of_line_anchor
    md = match('^a', "ba\na")
    assert !md.nil?
    assert_equal 'a', md[0]
    assert_equal 3, md.byte_begin(0)
  end

  def test_end_of_line_anchor
    md = match('a$', "ba\na")
    assert !md.nil?
    assert_equal 'a', md[0]
  end

  def test_begin_of_string_anchor
    md = match('\\Afoo', 'foobar')
    assert !md.nil?
    md2 = match('\\Afoo', 'xfoo')
    assert_nil md2
  end

  def test_end_of_string_strict_anchor
    md = match('foo\\z', 'foo')
    assert !md.nil?
    md2 = match('foo\\z', "foo\n")
    assert_nil md2
  end

  def test_word_boundary
    md = match('\\bcat\\b', 'a cat b')
    assert !md.nil?
    assert_equal 'cat', md[0]
  end

  def test_word_boundary_no_match_mid_word
    md = match('\\bcat\\b', 'bobcat')
    assert_nil md
  end

  # ========================================================================
  # \K keep
  # ========================================================================

  def test_keep_resets_match_start
    md = match('foo\\Kbar', 'foobar')
    assert !md.nil?
    assert_equal 'bar', md[0]
    assert_equal 'foo', md.pre_match
  end

  # ========================================================================
  # match? and =~
  # ========================================================================

  def test_match_predicate
    re = Naraku::Regexp.new('ab+')
    assert_equal true, re.match?('xxabbb')
    assert_equal false, re.match?('xxaccc')
  end

  def test_match_operator
    re = Naraku::Regexp.new('ab+')
    pos = re =~ 'xxabbb'
    assert_equal 2, pos
    assert_nil re =~ 'xxaccc'
  end

  # ========================================================================
  # byte_start parameter
  # ========================================================================

  def test_byte_start_skips_prefix
    re = Naraku::Regexp.new('abc')
    md = re.match('abcabc', 3)
    assert !md.nil?
    assert_equal 3, md.byte_begin(0)
  end

  # ========================================================================
  # Compile errors for unsupported features
  # ========================================================================

  def test_compile_error_lookahead
    assert_compile_error('(?=foo)', 'lookaround')
  end

  def test_compile_error_lookbehind
    assert_compile_error('(?<=foo)', 'lookaround')
  end

  def test_compile_error_back_ref
    assert_compile_error('(a)\\1', 'back reference')
  end

  def test_compile_error_atomic_group
    assert_compile_error('(?>a+)', 'atomic')
  end

  def test_compile_error_possessive_quantifier
    assert_compile_error('a*+', 'possessive')
  end

  # ========================================================================
  # MatchData helpers
  # ========================================================================

  def test_match_data_size
    md = match('(a)(b)', 'ab')
    assert !md.nil?
    assert_equal 3, md.size
    assert_equal 3, md.length
  end

  def test_match_data_to_s
    md = match('abc', 'xabcy')
    assert !md.nil?
    assert_equal 'abc', md.to_s
  end

  def test_match_data_pre_post_match
    md = match('b+', 'aabbc')
    assert !md.nil?
    assert_equal 'bb', md[0]
    assert_equal 'aa', md.pre_match
    assert_equal 'c', md.post_match
  end

  # ========================================================================
  # UTF-8 multibyte
  # ========================================================================

  def test_utf8_literal
    md = match('Ω', 'xΩy')
    assert !md.nil?
    assert_equal 'Ω', md[0]
  end

  def test_utf8_char_class
    md = match('[あ-お]+', 'xあいうえおy')
    assert !md.nil?
    assert_equal 'あいうえお', md[0]
  end

  def test_utf8_word_boundary
    md = match('\\bΩ\\b', ' Ω ')
    assert !md.nil?
    assert_equal 'Ω', md[0]
  end

  # ========================================================================
  # Empty-matchable loop guard (ReDoS safety)
  # ========================================================================

  def test_empty_matchable_loop_terminates
    md = match('(a*)*b', 'aaab')
    assert !md.nil?
    assert_equal 'aaab', md[0]
  end

  def test_empty_matchable_loop_no_match_terminates
    md = match('(a*)*b', 'aaac')
    assert_nil md
  end
end
