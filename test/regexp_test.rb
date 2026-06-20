class RegexpTest < Mtest::Test
  def match(pattern, subject, byte_start: 0, **parser_options)
    Naraku::Regexp.new(pattern, **parser_options).match(subject, byte_start)
  end

  def assert_compile_error(pattern, error_substr, enc = Naraku::Encoding::UTF_8, **parser_options)
    begin
      Naraku::Regexp.new(pattern, enc, **parser_options)
    rescue Naraku::CompileError => e
      assert e.message.include?(error_substr), "expected #{e.message.inspect} to include #{error_substr.inspect}"
      return
    end
    assert false, "expected Naraku::CompileError for #{pattern.inspect}"
  end

  def assert_parse_error(pattern, error_substr, enc = Naraku::Encoding::UTF_8, **parser_options)
    begin
      Naraku::Regexp.new(pattern, enc, **parser_options)
    rescue Naraku::ParseError => e
      assert e.message.include?(error_substr), "expected #{e.message.inspect} to include #{error_substr.inspect}"
      return
    end
    assert false, "expected Naraku::ParseError for #{pattern.inspect}"
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
  # CC1: POSIX character classes
  # Onigmo bug: 20-char lookahead heuristic causes O(n²) parse + confusing errors.
  # Naraku: linear parse, unknown names always raise ParseError.
  # ========================================================================

  def test_posix_class_alpha_digit
    # CC1: valid POSIX classes parse and match correctly
    md = match('[[:alpha:]][[:digit:]]+', 'abc x5z')
    assert !md.nil?
    assert_equal 'x5', md[0]
  end

  def test_posix_class_unknown_raises_parse_error
    # CC1: unknown POSIX class name must raise ParseError (not a fallback)
    assert_parse_error('[[:unknownclass:]]', 'invalid POSIX character class name')
  end

  # ========================================================================
  # \h / \H — hex digit
  # ========================================================================

  def test_hex_digit_h_matches
    md = match('\\h+', 'zff3z')
    assert !md.nil?
    assert_equal 'ff3', md[0]
  end

  def test_non_hex_digit_matches_non_hex
    md = match('\\H+', 'zzff3')
    assert !md.nil?
    assert_equal 'zz', md[0]
  end

  def test_hex_digit_h_no_match
    assert_nil match('\\h+', 'xyz')
  end

  def test_non_hex_digit_no_match_all_hex
    assert_nil match('\\H+', 'ff3a0')
  end

  def test_hex_digit_h_uppercase
    md = match('\\h+', 'zAF9z')
    assert !md.nil?
    assert_equal 'AF9', md[0]
  end

  def test_hex_digit_h_in_char_class
    md = match('[\\h]+', 'x1f2z')
    assert !md.nil?
    assert_equal '1f2', md[0]
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

  def test_compile_error_absence_group
    assert_compile_error('(?~a+)', 'absence')
  end

  def test_compile_error_subexp_call
    assert_compile_error('(a)\\g<1>', 'sub-expression')
  end

  # GC1 regression: \X must raise CompileError until dedicated VM instruction exists.
  # Onigmo expands \X to the UAX#29 regex (no error). Naraku rejects it explicitly.
  def test_compile_error_grapheme_cluster
    assert_compile_error('\\X', 'unsupported feature')
  end

  # ========================================================================
  # (?x) extended mode
  # ========================================================================

  def test_extended_mode_ignores_spaces
    md = match('a  b  c', 'xabcz', is_extended_mode: true)
    assert !md.nil?
    assert_equal 'abc', md[0]
  end

  def test_extended_mode_ignores_hash_comment
    md = match("a # match a\nb # then b", 'xabz', is_extended_mode: true)
    assert !md.nil?
    assert_equal 'ab', md[0]
  end

  def test_extended_mode_inline_flag
    md = match('(?x)a  b', 'xaby')
    assert !md.nil?
    assert_equal 'ab', md[0]
  end

  # ========================================================================
  # (?#...) inline comment
  # ========================================================================

  def test_inline_comment_is_ignored
    md = match('a(?#this is a comment)b', 'xaby')
    assert !md.nil?
    assert_equal 'ab', md[0]
  end

  def test_inline_comment_multiple
    md = match('(?#start)a(?#middle)b(?#end)', 'ab')
    assert !md.nil?
    assert_equal 'ab', md[0]
  end

  def test_inline_comment_with_special_chars
    md = match('\\d(?#digits)\\w', 'x1ay')
    assert !md.nil?
    assert_equal '1a', md[0]
  end

  # ========================================================================
  # \R — Unicode newline sequence
  # ========================================================================

  def test_newline_r_matches_lf
    md = match('\\R', "a\nb")
    assert !md.nil?
    assert_equal "\n", md[0]
  end

  def test_newline_r_matches_cr
    md = match('\\R', "a\rb")
    assert !md.nil?
    assert_equal "\r", md[0]
  end

  def test_newline_r_matches_crlf_as_unit
    md = match('\\R', "a\r\nb")
    assert !md.nil?
    assert_equal "\r\n", md[0]
  end

  def test_newline_r_crlf_priority_over_cr
    # \R should consume \r\n together, not just \r
    md = match('\\Rx', "\r\nx")
    assert !md.nil?
    assert_equal "\r\nx", md[0]
  end

  def test_newline_r_matches_vt
    md = match('\\R', "a\vb")
    assert !md.nil?
    assert_equal "\v", md[0]
  end

  def test_newline_r_matches_ff
    md = match('\\R', "a\fb")
    assert !md.nil?
    assert_equal "\f", md[0]
  end

  def test_newline_r_no_match_non_newline
    assert_nil match('\\R', 'abc')
  end

  def test_newline_r_plus_quantifier
    md = match('a\\R+b', "a\r\n\nb")
    assert !md.nil?
    assert_equal "a\r\n\nb", md[0]
  end

  def test_newline_r_in_alternation
    md = match('\\R|x', 'ax')
    assert !md.nil?
    assert_equal 'x', md[0]
  end

  # ========================================================================
  # (?>...) — atomic groups
  # ========================================================================

  def test_atomic_group_basic_match
    md = match('(?>a+)b', 'aaab')
    assert !md.nil?
    assert_equal 'aaab', md[0]
  end

  def test_atomic_group_prevents_backtrack
    # (?>a+) greedily consumes all a's; trailing a has nothing left to match
    assert_nil match('(?>a+)a', 'aaa')
  end

  def test_atomic_group_vs_non_atomic
    # non-atomic: NFA explores both "abc" and "ab" alternatives — falls back to "ab" then 'c'
    assert_equal 'abc', match('(?:abc|ab)c', 'abc')[0]
    # atomic: commits to the first-priority branch "abc"; no fallback to "ab"
    assert_nil match('(?>abc|ab)c', 'abc')
  end

  def test_atomic_group_with_star
    md = match('(?>a*)b', 'aaab')
    assert !md.nil?
    assert_equal 'aaab', md[0]
  end

  def test_atomic_group_empty_match
    md = match('(?>a*)b', 'b')
    assert !md.nil?
    assert_equal 'b', md[0]
  end

  def test_atomic_group_no_match
    assert_nil match('(?>b+)', 'aaa')
  end

  def test_atomic_group_mid_string
    md = match('x(?>a+)y', 'xaaay')
    assert !md.nil?
    assert_equal 'xaaay', md[0]
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

  # [ß]/i — char class with multi-char fold (CF3)
  def test_full_fold_char_class_eszett_matches_ss
    assert_equal 'ss', match_f("[\xC3\x9F]", 'xssy')[0]
  end

  def test_full_fold_char_class_eszett_matches_upper_ss
    assert_equal 'SS', match_f("[\xC3\x9F]", 'xSSy')[0]
  end

  def test_full_fold_char_class_eszett_matches_mixed_ss
    assert_equal 'Ss', match_f("[\xC3\x9F]", 'xSsy')[0]
  end

  def test_full_fold_char_class_eszett_matches_itself
    assert_equal "\xC3\x9F", match_f("[\xC3\x9F]", "x\xC3\x9Fy")[0]
  end

  def test_full_fold_char_class_eszett_no_match_single_s
    assert_nil match_f("[\xC3\x9F]", 'xsy')
  end

  # ß does not match a single 's' (fold of ß is ss, not s)
  def test_full_fold_eszett_no_match_single_s
    assert_nil match_f("\xC3\x9F", 'xsy')
  end

  def test_full_fold_no_match
    assert_nil match_f("\xC3\x9F", 'xyz')
  end

  # compile_char_type and compile_char_prop use the same multi-char fold path
  def test_full_fold_char_type_word_basic
    assert_equal 'hello', match_f('\\w+', 'hello')[0]
  end

  # \p{Lu} contains ẞ (U+1E9E); simple fold expand adds ß (U+00DF)
  def test_full_fold_char_prop_uppercase_includes_eszett_lower
    assert_equal "\xC3\x9F", match_f('\\p{Lu}', "\xC3\x9F")[0]
  end

  # CF6 regression: char class must NOT be treated as literal for multi-char fold.
  # Onigmo bug: [s]s /Full fold matches ß (single-element class treated as literal s).
  # Naraku fix: [s]s and s[s] are both nil — char class stays a char class.
  def test_full_fold_cf6_char_class_not_treated_as_literal
    assert_nil match_f('[s]s', "\xC3\x9F") # ß (U+00DF)
    assert_nil match_f('s[s]', "\xC3\x9F")
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
  # CF7: case folding + \w + set intersection (Kelvin Sign U+212A)
  # ========================================================================

  # [k&&\w]/i — single-char class {k} intersected with \w.
  # Both Onigmo and Naraku correctly include Kelvin Sign in the expansion.
  def test_simple_fold_cf7_single_char_w_intersection_kelvin
    kelvin = "\xe2\x84\xaa" # K (U+212A KELVIN SIGN)
    assert !match_s('[k&&\w]', kelvin).nil?
  end

  # [a-z&&\w]/i — range-based class intersected with \w.
  # Onigmo bug: asc_cc suppressed by \w → Kelvin excluded (false).
  # Naraku fix: Kelvin correctly included (true).
  def test_simple_fold_cf7_range_w_intersection_kelvin
    kelvin = "\xe2\x84\xaa" # K (U+212A KELVIN SIGN)
    assert !match_s('[a-z&&\w]', kelvin).nil?
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

  # CF2 regression: Full fold + backreference.
  # Onigmo bug: /(ß)\1/i =~ "ssß" returns nil (multi-char fold not handled in backref).
  # Naraku fix: backref matching respects full case fold — "ssß" matches "(ß)\1".
  def test_back_ref_full_fold_cf2_eszett
    md = match("(\xC3\x9F)\\1", "ss\xC3\x9F", is_ignore_case: true, fold_flags: [:full])
    assert !md.nil?
    assert_equal "ss\xC3\x9F", md[0]
  end

  def test_back_ref_full_fold_cf2_eszett_literal_still_works
    # ßß/i matching ssß works in Onigmo too; Naraku must also pass it
    md = match("\xC3\x9F\xC3\x9F", "ss\xC3\x9F", is_ignore_case: true, fold_flags: [:full])
    assert !md.nil?
  end

  # CF3 regression: char class + Full fold + backref causing ReDoS in Onigmo.
  # Onigmo: /(x)[abcß]+\1/i on "x"+"ss"*30 causes exponential backtracking.
  # Naraku (Pike VM): O(n×m) time — guaranteed to finish instantly.
  def test_full_fold_cf3_redos_safe
    pattern = "(x)[abc\xC3\x9F]+\\1"
    subject = "x#{'ss' * 30}"
    assert_nil match(pattern, subject, is_ignore_case: true, fold_flags: [:full])
  end

  # CP1 regression: capture + lookahead empty-string loop caused infinite loop in Onigmo.
  # /((?=(a)))*/ =~ "a" hangs in Onigmo. Naraku terminates.
  def test_capture_lookahead_empty_loop_cp1_terminates
    md = match('((?=(a)))*', 'a')
    assert !md.nil?
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

  # LB1 regression: Onigmo rejects variable-length lookbehind (only fixed-width allowed).
  # Naraku supports unlimited lookbehind.
  def test_lookbehind_variable_length
    md = match('(?<=a+)b', 'aaab')
    assert !md.nil?
    assert_equal 'b', md[0]
  end

  def test_lookbehind_variable_length_no_match
    assert_nil match('(?<=a+)b', 'b')
  end

  def test_lookbehind_alternation_different_lengths
    # Onigmo: error unless all alternates have equal fixed length
    # Naraku: works — each alternate may have different length
    md = match('(?<=foo|ba)r', 'foor')
    assert !md.nil?
    assert_equal 'r', md[0]

    md2 = match('(?<=foo|ba)r', 'bar')
    assert !md2.nil?
    assert_equal 'r', md2[0]
  end

  # CF1 regression: lookbehind + Full fold failed in Onigmo.
  # /(?<=ß)/i =~ "ß" returns nil in Onigmo (fixed-length check fails after fold expansion).
  # Naraku: unlimited lookbehind correctly handles folded lookbehind.
  def test_lookbehind_full_fold_cf1
    md = match("(?<=\xC3\x9F)", "\xC3\x9F", is_ignore_case: true, fold_flags: [:full])
    assert !md.nil?
    assert_equal '', md[0]
  end

  # ========================================================================
  # Possessive quantifiers (a*+, a++, a?+, a{m,n}+)
  # ========================================================================

  def test_possessive_star_match
    md = match('a*+b', 'aaab')
    assert_equal 'aaab', md[0]
  end

  def test_possessive_star_zero_match
    md = match('a*+b', 'b')
    assert_equal 'b', md[0]
  end

  def test_possessive_star_no_backtrack
    # a*+ commits to "aaa", then a fails at pos=3 → no match
    assert_nil match('a*+a', 'aaa')
  end

  def test_possessive_plus_match
    md = match('a++b', 'aaab')
    assert_equal 'aaab', md[0]
  end

  def test_possessive_plus_no_match_no_a
    assert_nil match('a++', 'b')
  end

  def test_possessive_plus_no_backtrack
    # a++ commits to "aaa", then a at pos=3 fails → no match
    assert_nil match('a++a', 'aaa')
  end

  def test_possessive_question_with_char
    md = match('a?+b', 'ab')
    assert_equal 'ab', md[0]
  end

  def test_possessive_question_zero
    md = match('a?+b', 'b')
    assert_equal 'b', md[0]
  end

  def test_possessive_question_no_backtrack
    # a?+ commits to "a", then a at pos=1 fails → no match
    assert_nil match('a?+a', 'a')
  end

  def test_possessive_bounded_max
    md = match('a{2,4}+', 'aaaaa')
    assert_equal 'aaaa', md[0]
  end

  def test_possessive_bounded_min_fail
    assert_nil match('a{2,4}+b', 'ab')
  end

  def test_possessive_char_class
    md = match('[a-z]++', 'hello world')
    assert_equal 'hello', md[0]
  end

  def test_possessive_dot
    md = match('.++', 'abc')
    assert_equal 'abc', md[0]
  end

  def test_possessive_group
    md = match('(ab)++', 'ababc')
    assert_equal 'abab', md[0]
  end

  def test_possessive_mid_string
    md = match('\\d++', 'abc123def')
    assert_equal '123', md[0]
  end

  def test_possessive_digit_no_backtrack
    # \d++ commits to "123", then \d at end fails
    assert_nil match('\\d++\\d', '123')
  end

  # ========================================================================
  # Named captures (?<name>...)
  # ========================================================================

  def test_named_capture_integer_index
    md = match('(?<year>\\d{4})-(?<month>\\d{2})', '2024-06')
    assert !md.nil?
    assert_equal '2024-06', md[0]
    assert_equal '2024',    md[1]
    assert_equal '06',      md[2]
  end

  def test_named_capture_string_key
    md = match('(?<year>\\d{4})-(?<month>\\d{2})', '2024-06')
    assert !md.nil?
    assert_equal '2024', md['year']
    assert_equal '06',   md['month']
  end

  def test_named_capture_symbol_key
    md = match('(?<year>\\d{4})-(?<month>\\d{2})', '2024-06')
    assert !md.nil?
    assert_equal '2024', md[:year]
    assert_equal '06',   md[:month]
  end

  def test_named_capture_unknown_key
    md = match('(?<word>\\w+)', 'hello')
    assert !md.nil?
    assert_nil md['missing']
    assert_nil md[:missing]
  end

  def test_named_capture_names
    md = match('(?<year>\\d{4})-(?<month>\\d{2})', '2024-06')
    assert !md.nil?
    assert_equal %w[year month].sort, md.names.sort
  end

  def test_named_capture_named_captures
    md = match('(?<year>\\d{4})-(?<month>\\d{2})', '2024-06')
    assert !md.nil?
    nc = md.named_captures
    assert_equal '2024', nc['year']
    assert_equal '06',   nc['month']
  end

  def test_named_capture_with_anonymous
    md = match('(?<a>\\w+)-(\\d+)-(?<b>\\w+)', 'foo-42-bar')
    assert !md.nil?
    assert_equal 'foo-42-bar', md[0]
    assert_equal 'foo',        md['a']
    assert_equal 'bar',        md['b']
  end

  def test_named_capture_unmatched_nil
    md = match('(?<a>x)?(?<b>y)', 'y')
    assert !md.nil?
    assert_nil md['a']
    assert_nil md[:a]
    assert_equal 'y', md['b']
  end

  def test_named_capture_duplicate_names
    # (?<a>x)|(?<a>y) — same name appears twice; last matched group wins
    md = match('(?<a>x)|(?<a>y)', 'y')
    assert !md.nil?
    assert_equal 'y', md['a']
    assert_equal 'y', md[:a]
  end

  # ========================================================================
  # MatchData#inspect
  # ========================================================================

  def test_match_data_inspect_no_captures
    md = match('ab', 'xaby')
    assert_equal '#<MatchData "ab">', md.inspect
  end

  def test_match_data_inspect_numbered_captures
    md = match('(a)(b)', 'ab')
    assert_equal '#<MatchData "ab" 1:"a" 2:"b">', md.inspect
  end

  def test_match_data_inspect_named_captures
    md = match('(?<x>a)(?<y>b)', 'ab')
    assert_equal '#<MatchData "ab" x:"a" y:"b">', md.inspect
  end

  def test_match_data_inspect_unmatched_capture
    md = match('(a)?(b)', 'b')
    assert_equal '#<MatchData "b" 1:nil 2:"b">', md.inspect
  end

  # ========================================================================
  # MatchData#values_at
  # ========================================================================

  def test_match_data_values_at
    md = match('(a)(b)(c)', 'abc')
    assert_equal %w[abc a c], md.values_at(0, 1, 3)
  end

  def test_match_data_values_at_named
    md = match('(?<x>a)(?<y>b)', 'ab')
    assert_equal %w[a b], md.values_at('x', 'y')
  end

  # ========================================================================
  # Regexp#source / Regexp#inspect
  # ========================================================================

  def test_regexp_source
    re = Naraku::Regexp.new('ab+c')
    assert_equal 'ab+c', re.source
  end

  def test_regexp_inspect
    re = Naraku::Regexp.new('ab+c')
    assert_equal '/ab+c/', re.inspect
  end

  def test_regexp_inspect_escapes_slash
    re = Naraku::Regexp.new('a/b')
    assert_equal '/a\\/b/', re.inspect
  end

  # ========================================================================
  # Regexp#===
  # ========================================================================

  def test_regexp_case_equality_match
    re = Naraku::Regexp.new('\\d+')
    s = '123'
    matched = case s
              when re then true
              else false
              end
    assert matched
  end

  def test_regexp_case_equality_no_match
    re = Naraku::Regexp.new('\\d+')
    s = 'abc'
    matched = case s
              when re then true
              else false
              end
    assert !matched
  end

  # ========================================================================
  # Regexp#names / Regexp#named_captures
  # ========================================================================

  def test_regexp_names_empty
    re = Naraku::Regexp.new('(a)(b)')
    assert_equal [], re.names
  end

  def test_regexp_names
    re = Naraku::Regexp.new('(?<x>a)(?<y>b)')
    assert_equal %w[x y], re.names
  end

  def test_regexp_named_captures
    re = Naraku::Regexp.new('(?<x>a)(?<y>b)')
    nc = re.named_captures
    assert_equal [1], nc['x']
    assert_equal [2], nc['y']
  end

  # ========================================================================
  # ST2: invalid byte sequence handling at match time
  # Onigmo: behavior depends on encoding; Naraku raises on invalid UTF-8 bytes.
  # ========================================================================

  def test_invalid_utf8_raises_at_match_time
    re = Naraku::Regexp.new('.')
    raised = false
    begin
      re.match("\xff\xfe")
    rescue StandardError => e
      raised = true
      assert e.message.include?('invalid') || e.message.include?('byte'), e.message
    end
    assert raised, 'expected an error for invalid UTF-8 bytes'
  end

  # ========================================================================
  # Parse-time byte sequence errors (coverage: parse.c lines ~104, 109, 112)
  # ========================================================================

  def test_invalid_byte_sequence_in_us_ascii_pattern
    # Raw UTF-8 bytes are invalid in US_ASCII encoding
    assert_parse_error("\xC3\xA9", 'invalid byte sequence', Naraku::Encoding::US_ASCII)
  end

  def test_incomplete_byte_sequence_in_utf8_pattern
    # Lone lead byte 0xC3 with no continuation byte
    assert_parse_error("\xC3", 'incomplete byte sequence')
  end

  # ========================================================================
  # Unicode escape parse errors (coverage: parse.c lines ~454, 493, 510, 526,
  #                              537, 547, 557, 565, 604)
  # ========================================================================

  def test_unicode_escape_empty_braces_raises_parse_error
    # \u{} — no hex digits inside braces
    assert_parse_error('\\u{}', 'empty Unicode')
  end

  def test_unicode_escape_unclosed_brace_raises_parse_error
    # \u{41 — hex digits but no closing }
    assert_parse_error('\\u{41', 'unclosed Unicode')
  end

  def test_unicode_escape_code_point_out_of_range_raises_parse_error
    # \u{1FFFFF} — code point beyond U+10FFFF
    assert_parse_error('\\u{1FFFFF}', 'out of range')
  end

  def test_unicode_escape_bare_u_raises_parse_error
    # \u with no { following — treated as unclosed brace
    assert_parse_error('\\u', 'unclosed Unicode')
  end

  # ========================================================================
  # Group structure errors (coverage: parse.c lines ~3983, ~3363)
  # ========================================================================

  def test_unmatched_close_paren_raises_parse_error
    assert_parse_error(')', 'unmatched close parenthesis')
  end

  def test_unterminated_group_raises_parse_error
    assert_parse_error('(abc', 'unterminated group')
  end

  def test_too_many_capture_groups_raises_parse_error
    # Force limit to 2 and use 3 groups
    assert_parse_error('(a)(b)(c)', 'too many capture groups', max_capture_num_limit: 2)
  end

  # ========================================================================
  # Single-byte encoding coverage
  # (encoding_ascii.c: nk_enc_sb_scan_mbc_width, sb_encode_mbc, sb_decode_mbc)
  # (encoding/us_ascii.c: us_ascii_encode_mbc success path)
  # ========================================================================

  def test_us_ascii_literal_pattern_matches
    # Covers us_ascii_encode_mbc (code < 128 path)
    re = Naraku::Regexp.new('abc', Naraku::Encoding::US_ASCII)
    assert_equal 'abc', re.match('xabcx')[0]
  end

  def test_ascii_8bit_literal_pattern_matches
    # Covers nk_enc_sb_scan_mbc_width and nk_enc_sb_decode_mbc (via regex_compile.c)
    re = Naraku::Regexp.new('hello', Naraku::Encoding::ASCII_8BIT)
    assert_equal 'hello', re.match('say hello')[0]
  end

  def test_ascii_8bit_high_byte_literal_matches
    # Covers nk_enc_sb_encode_mbc for code 0xE9 (Latin é)
    re = Naraku::Regexp.new("\xE9", Naraku::Encoding::ASCII_8BIT)
    m = re.match("caf\xE9")
    assert m, 'expected match for high byte 0xE9 in ASCII_8BIT pattern'
    assert_equal [233], m[0].bytes.to_a
  end

  def test_ascii_8bit_unsupported_char_prop_raises_compile_error
    # Covers nk_enc_ascii_get_cprop_code_range NK_ERR_UNSUPPORTED_CHAR_PROPERTY path
    assert_compile_error('\\p{Lu}', 'unsupported', Naraku::Encoding::ASCII_8BIT)
  end

  def test_us_ascii_escape_seq_creates_code_node_and_encodes
    # \n → TK_CODE(10); nk_enc_encode_mbc inline shortcut handles code < 128 directly
    re = Naraku::Regexp.new('a\\nb', Naraku::Encoding::US_ASCII)
    m = re.match("a\nb")
    assert m, 'expected match'
    assert_equal [97, 10, 98], m[0].bytes.to_a
  end

  def test_bitset_widening_handles_mid_range_state_count
    # (?:a?){30}a{30} compiles to ~90 NFA states, which exceeds the old 63-state
    # bitset limit. Regression test for the 64-127 state bitset path enabled by
    # widening goto_mask/initial_mask to nk_bitset128_t (Cycle N).
    re = Naraku::Regexp.new('(?:a?){30}a{30}')
    assert re.match?('a' * 30), 'expected match for exactly 30 a chars'
    assert re.match?("#{'a' * 30}b"), 'expected match for 30 a chars followed by other text'
    assert !re.match?('a' * 29), 'expected no match for only 29 a chars'
  end

  def test_bitset_128_fallback_beyond_127_states_still_correct
    # (?:a?){90}a{90} compiles to far more than 127 NFA states, so goto_mask
    # stays NULL and the general thread-list executor handles it. Regression
    # test ensuring that fallback boundary is still correct after raising the
    # bitset limit from 63 to 127.
    re = Naraku::Regexp.new('(?:a?){90}a{90}')
    assert re.match?('a' * 90), 'expected match for exactly 90 a chars'
    assert !re.match?('a' * 89), 'expected no match for only 89 a chars'
  end

  def test_simd_ascii_run_scan_exact_16_byte_chunk_boundary
    # Cycle O: the SIMD run scan processes 16 bytes per chunk. Place the
    # non-member byte exactly at offsets 15, 16, and 17 to exercise the
    # chunk/tail boundary (run length == chunk size, one less, one more).
    md = match('\\d+', "#{'1' * 15}x")
    assert_equal '1' * 15, md[0]

    md = match('\\d+', "#{'1' * 16}x")
    assert_equal '1' * 16, md[0]

    md = match('\\d+', "#{'1' * 17}x")
    assert_equal '1' * 17, md[0]
  end

  def test_simd_ascii_run_scan_spans_multiple_chunks
    # A run much longer than one 16-byte SIMD chunk, for a char class with
    # multiple ASCII ranges ([a-zA-Z0-9] decomposes into 3 simd_ranges).
    md = match('[a-zA-Z0-9]+', "#{'aB3' * 20}!!!")
    assert_equal 'aB3' * 20, md[0]
  end

  def test_simd_ascii_run_scan_falls_back_for_many_disjoint_ranges
    # More than NK_CC_SIMD_MAX_RANGES (4) contiguous ASCII runs disables the
    # SIMD path (simd_range_count reset to 0 in program_add_char_class);
    # the scalar ascii_lookup scan must still produce correct results.
    md = match('[ace gik mo]+', 'aceg ikmo aceg ikmoXX')
    assert !md.nil?
    assert_equal 'aceg ikmo aceg ikmo', md[0]
  end

  def test_simd_ascii_run_scan_stops_before_multibyte_char
    # The run scan must stop cleanly at a UTF-8 continuation byte boundary
    # rather than reading past it as if it were an ASCII class member.
    md = match('\\w+', "#{'a' * 16}ジ")
    assert_equal 'a' * 16, md[0]
  end

  def test_bitset_fixed_point_run_scan_matches_and_non_matches
    # search_impl_bitset's single-byte repetition shortcut (fixed point:
    # next == active) targets patterns like a+b that don't qualify for the
    # is_pure_char_class_plus bypass. match? (no_caps) is what dispatches to
    # search_impl_bitset, so exercise it directly via match?.
    re = Naraku::Regexp.new('a+b')
    assert re.match?('aaab'), 'expected match for a run of a followed by b'
    assert re.match?('ab'), 'expected match for a single a followed by b'
    assert !re.match?('aaa'), 'expected no match without a trailing b'
    assert !re.match?('bbb'), 'expected no match without any leading a'
  end

  def test_first_byte_table_handles_default_fold_match_p
    # Regression test for a pre-existing bug (predates this session, found
    # while testing the run-scan shortcut above): compute_first_byte_table
    # only registered a state's case-swapped counterpart byte when
    # fold_flags was NK_FOLD_ASCII_ONLY. Under the *default* simple-Unicode
    # fold (is_ignore_case: true with no fold_flags), `table['A']` was never
    # set for a state matching 'a', so search_impl_bitset's first-byte jump
    # (only reachable via match?, which dispatches to search_impl_bitset)
    # incorrectly treated any all-uppercase subject as unable to start a
    # match. match (the capturing path, which never uses search_impl_bitset)
    # was unaffected, which is why this had gone unnoticed.
    re = Naraku::Regexp.new('a', is_ignore_case: true)
    assert re.match?('A'), 'expected match? to find the case-folded literal'
    assert re.match('A'), 'expected match to find the case-folded literal'

    re2 = Naraku::Regexp.new('a+b', is_ignore_case: true)
    assert re2.match?('AAB'), 'expected match? on an all-uppercase repeated run'
  end

  def test_bitset_fixed_point_run_scan_respects_case_fold
    # The run scan only ever extends a run using the exact byte already
    # observed (no fold-equivalence claims), so a case-insensitive pattern
    # must still match mixed-case runs correctly via the slower per-byte path
    # once the fast run stops at a differently-cased byte.
    re = Naraku::Regexp.new('a+b', is_ignore_case: true)
    assert re.match?('AaAb'), 'expected case-insensitive match across mixed-case run'
    assert re.match?('AAAB'), 'expected case-insensitive match for all-uppercase run'
  end

  def test_bitset_fixed_point_run_scan_with_anchor
    # Anchored patterns skip the start_active re-injection in
    # search_impl_bitset; the fixed-point run scan must still produce correct
    # results in that branch.
    re = Naraku::Regexp.new('\\Aa+b')
    assert re.match?('aaab'), 'expected anchored match for a run of a followed by b'
    assert !re.match?('xaaab'), 'expected no anchored match when a leading char precedes the run'
  end
end
