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

  # ========================================================================
  # ASCII-only case folding (A flag: is_ignore_case + fold_flags: [:ascii_only])
  # ========================================================================

  def match_a(pattern, subject)
    match(pattern, subject, is_ignore_case: true, fold_flags: [:ascii_only])
  end

  # Literal: basic upper/lower matching (md[0] reflects the subject, not pattern)
  def test_ascii_fold_literal_lower_input
    assert_equal 'HELLO', match_a('hello', 'HELLO')[0]
  end

  def test_ascii_fold_literal_upper_pattern
    assert_equal 'hello', match_a('HELLO', 'hello')[0]
  end

  def test_ascii_fold_literal_mixed
    assert_equal 'Watson', match_a('watson', 'xWatsonY')[0]
  end

  def test_ascii_fold_literal_no_match
    assert_nil match_a('hello', 'world')
  end

  # Non-ASCII bytes are not folded
  def test_ascii_fold_literal_non_ascii_unaffected
    assert_nil match_a('ss', "\xC3\x9F") # ß (U+00DF) should NOT match
  end

  # Capture group: position is correct
  def test_ascii_fold_capture_position
    md = match_a('(ab)', 'xABy')
    assert_equal 'AB', md[0]
    assert_equal 1, md.byte_begin(0)
    assert_equal 3, md.byte_end(0)
    assert_equal 'AB', md[1]
  end

  # Char class: [a-z] with fold matches A-Z too
  def test_ascii_fold_char_class_lower_range
    md = match_a('[a-z]+', 'ABC')
    assert_equal 'ABC', md[0]
  end

  def test_ascii_fold_char_class_upper_range
    md = match_a('[A-Z]+', 'abc')
    assert_equal 'abc', md[0]
  end

  def test_ascii_fold_char_class_mixed_range
    md = match_a('[a-zA-Z]+', 'Hello123')
    assert_equal 'Hello', md[0]
  end

  def test_ascii_fold_char_class_no_match
    assert_nil match_a('[a-z]+', '123')
  end

  # Alternation
  def test_ascii_fold_alternation
    assert_equal 'FOO', match_a('foo|bar', 'xFOOy')[0]
    assert_equal 'BAR', match_a('foo|bar', 'xBARy')[0]
  end

  # Quantifier: a+ with fold matches a run of A/a only (not B, C, ...)
  def test_ascii_fold_quantifier_plus
    assert_equal 'AA', match_a('a+', 'xAABy')[0]
    assert_equal 'aa', match_a('A+', 'xaaBy')[0]
  end

  # Run-scan optimisation: long mixed-case run is scanned without per-char NFA step
  def test_ascii_fold_run_scan_long
    input = 'AaAaAaAaAaAaAaAaAaAa'
    assert_equal input, match_a('a+', input)[0]
    assert_equal input, match_a('A+', input)[0]
  end

  # CF6 regression: [s]s must NOT behave like ss for fold purposes.
  # With ASCII-only fold, [s] is a char class (not a literal), so
  # /[s]s/A does NOT match "ß" (ß is non-ASCII and never folded).
  def test_ascii_fold_cf6_char_class_not_treated_as_literal
    assert_nil match_a('[s]s', "\xC3\x9F")
    assert_nil match_a('s[s]', "\xC3\x9F")
  end

  # ========================================================================
  # Full (1-to-many Unicode) case folding
  # ========================================================================
  # ß (U+00DF) folds to ss under NK_FOLD_FULL.  Pattern ß should match ß
  # itself plus all uppercase/lowercase variants of the two-char sequence ss.

  def match_f(pattern, subject)
    match(pattern, subject, is_ignore_case: true, fold_flags: [:full])
  end

  def test_full_fold_eszett_matches_itself
    assert_equal "\xC3\x9F", match_f("\xC3\x9F", "x\xC3\x9Fy")[0]
  end

  def test_full_fold_eszett_matches_ss
    assert_equal 'ss', match_f("\xC3\x9F", 'xssy')[0]
  end

  def test_full_fold_eszett_matches_upper_lower_s
    assert_equal 'Ss', match_f("\xC3\x9F", 'xSsy')[0]
  end

  def test_full_fold_eszett_matches_lower_upper_s
    assert_equal 'sS', match_f("\xC3\x9F", 'xsSy')[0]
  end

  def test_full_fold_eszett_matches_upper_ss
    assert_equal 'SS', match_f("\xC3\x9F", 'xSSy')[0]
  end

  # Pattern ss should match ß and all case variants
  def test_full_fold_ss_matches_eszett
    assert_equal "\xC3\x9F", match_f('ss', "x\xC3\x9Fy")[0]
  end

  def test_full_fold_ss_matches_upper_ss
    assert_equal 'SS', match_f('ss', 'xSSy')[0]
  end

  # ASCII letters still fold under full fold
  def test_full_fold_ascii_letters
    assert_equal 'HELLO', match_f('hello', 'HELLO')[0]
  end

  # Mixed literal: pattern i + ß should match iss, ISS, etc.
  def test_full_fold_mixed_literal_lower
    assert_equal "i\xC3\x9F", match_f("i\xC3\x9F", "xi\xC3\x9Fy")[0]
  end

  def test_full_fold_mixed_literal_ss
    assert_equal 'iss', match_f("i\xC3\x9F", 'xissy')[0]
  end

  def test_full_fold_mixed_literal_upper
    assert_equal 'ISS', match_f("i\xC3\x9F", 'xISSy')[0]
  end

  # Char class with full fold: 1:1 fold pairs are still expanded
  def test_full_fold_char_class_simple_pair
    assert_equal "\xC3\x84", match_f("[\xC3\xA4]", "x\xC3\x84y")[0]
  end

  # ß does not match a single 's' (fold of ß is ss, not s)
  def test_full_fold_eszett_no_match_single_s
    assert_nil match_f("\xC3\x9F", 'xsy')
  end

  def test_full_fold_no_match
    assert_nil match_f("\xC3\x9F", 'xyz')
  end

  # ========================================================================
  # Simple (1-to-1 Unicode) case folding — default i flag
  # ========================================================================

  def match_s(pattern, subject)
    match(pattern, subject, is_ignore_case: true)
  end

  # ä (U+00E4) ↔ Ä (U+00C4): simple fold pair in Latin Extended-A
  def test_simple_fold_literal_lower_to_upper
    assert_equal 'Ä', match_s('ä', 'xÄy')[0]
  end

  def test_simple_fold_literal_upper_to_lower
    assert_equal 'ä', match_s('Ä', 'xäy')[0]
  end

  def test_simple_fold_literal_no_match
    assert_nil match_s('ä', 'xyz')
  end

  # ASCII letters still fold under simple fold
  def test_simple_fold_ascii_letters
    assert_equal 'HELLO', match_s('hello', 'HELLO')[0]
  end

  # Char class with simple fold: [ä] should also match Ä
  def test_simple_fold_char_class
    md = match_s('[äÄ]+', 'xÄäy')
    assert_equal 'Ää', md[0]
  end

  # Non-ASCII chars without a case pair are unaffected
  def test_simple_fold_no_pair_unchanged
    assert_nil match_s('あ', 'い')
    assert_equal 'あ', match_s('あ', 'xあy')[0]
  end

  # Capture: byte positions are correct for multi-byte fold match
  def test_simple_fold_capture_positions
    md = match_s('(ä)', 'xÄy')
    assert_equal 'Ä', md[0]
    assert_equal 1, md.byte_begin(0)   # ä is 2 bytes, Ä starts at byte 1
    assert_equal 3, md.byte_end(0)     # Ä ends at byte 3
  end

  # ========================================================================
  # Back-references (\1, \k<name>)
  # ========================================================================

  def test_back_ref_single_char_match
    md = match('(a)\\1', 'xaay')
    assert_equal 'aa', md[0]
    assert_equal 'a',  md[1]
  end

  def test_back_ref_single_char_no_match
    assert_nil match('(a)\\1', 'xaby')
  end

  def test_back_ref_multi_char_match
    md = match('(abc)\\1', 'xxabcabcyy')
    assert_equal 'abcabc', md[0]
    assert_equal 'abc',    md[1]
  end

  def test_back_ref_multi_char_no_match
    assert_nil match('(abc)\\1', 'abcab')
  end

  def test_back_ref_with_continuation
    md = match('(ab)\\1c', 'ababc')
    assert_equal 'ababc', md[0]
  end

  def test_back_ref_with_continuation_no_match
    assert_nil match('(ab)\\1c', 'abab')
  end

  def test_back_ref_empty_capture_matches_empty
    md = match('(a?)\\1', 'b')
    assert !md.nil?
    assert_equal '', md[0]
    assert_equal '', md[1]
  end

  def test_back_ref_capture_positions
    md = match('(foo)\\1', 'xfoofoo')
    assert_equal 'foofoo', md[0]
    assert_equal 'foo',    md[1]
    assert_equal 1, md.byte_begin(1)
    assert_equal 4, md.byte_end(1)
  end

  def test_back_ref_named
    md = match('(?<word>\\w+)\\k<word>', 'hellohello world')
    assert_equal 'hellohello', md[0]
  end

  def test_back_ref_named_no_match
    # "abcdef" has no repeated consecutive \w sequence, so no match expected.
    assert_nil match('(?<word>\\w+)\\k<word>', 'abcdef')
  end

  def test_back_ref_case_insensitive_ascii
    md = match('(abc)\\1', 'ABCabc', is_ignore_case: true)
    assert_equal 'ABCabc', md[0]
  end

  def test_back_ref_case_insensitive_no_match
    assert_nil match('(abc)\\1', 'ABCxyz', is_ignore_case: true)
  end

  # ========================================================================
  # Lookahead (?=...) / (?!...)
  # ========================================================================

  def test_positive_lookahead_match
    md = match('foo(?=bar)', 'foobar')
    assert_equal 'foo', md[0]
  end

  def test_positive_lookahead_no_match
    assert_nil match('foo(?=bar)', 'foobaz')
  end

  def test_positive_lookahead_with_capture
    md = match('(\\w+)(?= world)', 'hello world')
    assert_equal 'hello', md[0]
    assert_equal 'hello', md[1]
  end

  def test_positive_lookahead_mid_string
    md = match('\\d+(?=px)', 'width:42px;')
    assert_equal '42', md[0]
  end

  def test_negative_lookahead_match
    md = match('foo(?!bar)', 'foobaz')
    assert_equal 'foo', md[0]
  end

  def test_negative_lookahead_no_match
    assert_nil match('foo(?!bar)', 'foobar')
  end

  def test_negative_lookahead_end_of_string
    md = match('foo(?!bar)', 'foo')
    assert_equal 'foo', md[0]
  end

  def test_positive_lookahead_alternation
    md = match('(cat|catch)(?=ing)', 'catching')
    assert_equal 'catch', md[0]
  end

  # ========================================================================
  # Lookbehind (?<=...) / (?<!...)
  # ========================================================================

  def test_positive_lookbehind_match
    md = match('(?<=foo)bar', 'foobar')
    assert_equal 'bar', md[0]
  end

  def test_positive_lookbehind_no_match
    assert_nil match('(?<=foo)bar', 'bazbar')
  end

  def test_positive_lookbehind_position
    md = match('(?<=\\d{3})\\w+', 'abc123def')
    assert_equal 'def', md[0]
  end

  def test_negative_lookbehind_match
    md = match('(?<!foo)bar', 'bazbar')
    assert_equal 'bar', md[0]
  end

  def test_negative_lookbehind_no_match
    assert_nil match('(?<!foo)bar', 'foobar')
  end

  def test_negative_lookbehind_at_start
    md = match('(?<!\\d)\\w+', 'hello')
    assert_equal 'hello', md[0]
  end

  def test_lookahead_and_lookbehind_combined
    md = match('(?<=\\()\\w+(?=\\))', '(hello)')
    assert_equal 'hello', md[0]
  end

  def test_lookahead_zero_width
    md = match('(?=\\w)', 'abc')
    assert_equal '', md[0]
    assert_equal 0, md.byte_begin(0)
    assert_equal 0, md.byte_end(0)
  end

  def test_lookbehind_zero_width
    md = match('(?<=a)', 'xay')
    assert_equal '', md[0]
    assert_equal 2, md.byte_begin(0)
    assert_equal 2, md.byte_end(0)
  end
end
