# frozen_string_literal: true

require_relative '../test_helper'

module NarakuRuby
  class DFATest < Minitest::Test
    def test_exposes_convenience_module_match_api
      assert_equal true, NarakuRuby::DFA.match?('ab+', 'xxabbb')
      assert_equal false, NarakuRuby::DFA.match?('ab+', 'xxaccc')
    end

    def test_matches_basic_literals_alternation_and_concatenation_like_ruby
      assert_match_data_like_ruby('ab|cd', ['', 'ab', 'cd', 'xab', 'acd', 'xxcdyy', 'ef'])
    end

    def test_matches_quantifiers_like_ruby
      assert_match_data_like_ruby('a{2,4}b+', ['', 'ab', 'aab', 'aaabbb', 'aaaab', 'aaaaab', 'baaab'])
      assert_match_data_like_ruby('a+?b', ['', 'ab', 'aaab', 'acb', 'b'])
      assert_match_data_like_ruby('(ab)*c', ['', 'c', 'abc', 'ababc', 'abab', 'zababc'])
    end

    def test_bounded_quantifier_optional_tail_is_capture_compatible
      assert_match_data_like_ruby('((a)|(ab)){1,3}b', 'aaab')
      assert_match_data_like_ruby('((a)?){1,3}a', 'aaa')
      assert_match_data_like_ruby('(a(b)?){1,3}c', 'abbc')
    end

    def test_additional_quantifier_match_data_compatibility
      assert_match_data_like_ruby('a*', 'aaaa')
      assert_match_data_like_ruby('a+', 'aaaa')
      assert_match_data_like_ruby('a?', 'aaaa')
      assert_match_data_like_ruby('(a){2,4}a', 'aaaaa')
      assert_match_data_like_ruby('(ab){0,2}c', 'ababc')
      assert_match_data_like_ruby('(ab){1,3}c', 'ababc')
      assert_match_data_like_ruby('(a?)*b', 'aaab')
      assert_match_data_like_ruby('(a+)*b', 'aaab')
      assert_match_data_like_ruby('(a*)*b', 'aaab')
      assert_match_data_like_ruby('(a{0,2})(b{1,3})', 'aaabbb')
    end

    def test_matches_char_class_dot_and_char_properties_like_ruby
      assert_match_data_like_ruby('[a-z]+', ['', 'ABC', 'abc', 'x1', '0z'])
      assert_match_data_like_ruby('\\d\\w\\p{Lu}', ['', '1aA', '1_A', '1aa', 'A1_A'])
      assert_match_data_like_ruby('a.b', %W[a\nb acb aab ab])
    end

    def test_matches_anchors_and_boundaries_like_ruby
      assert_match_data_like_ruby('^a$', ['', 'a', "a\n", "\na", 'ba', "x\na\ny"])
      assert_match_data_like_ruby('\\Afoo\\z', ['', 'foo', "foo\n", 'xfoo'])
      assert_match_data_like_ruby('\\Afoo\\Z', %W[foo foo\n foo\n\n xfoo])
      assert_match_data_like_ruby('\\bcat\\b', ['', 'cat', 'bobcat', 'cat!', 'a cat b', 'cat2'])
      assert_match_data_like_ruby('\\Bcat\\B', ['', 'cat', 'scatx', 'catx', 'xcat'])
      assert_match_data_like_ruby('\\bΩ\\b', ['', 'Ω', 'xΩy', ' Ω ', 'Ωx', 'xΩ'])
    end

    def test_matches_with_ignore_case_like_ruby
      assert_match_data_like_ruby(
        'ab[cd]',
        ['', 'abc', 'ABc', 'aBD', 'abE', 'zAbCz'],
        regexp_options: Regexp::IGNORECASE,
        parser_options: { is_ignore_case: true }
      )
    end

    def test_matches_ignore_case_literals_with_multi_char_fold_like_ruby
      assert_match_data_like_ruby(
        'ss',
        ['', 'ss', 'SS', 'ß', 'ẞ', 'xßy', 's', 'sss'],
        regexp_options: Regexp::IGNORECASE,
        parser_options: { is_ignore_case: true, fold_flags: [:full] }
      )
      assert_match_data_like_ruby(
        'Straße',
        ['', 'straße', 'STRASSE', 'Strasse', 'xStraßey', 'STRAẞE'],
        regexp_options: Regexp::IGNORECASE,
        parser_options: { is_ignore_case: true, fold_flags: [:full] }
      )
    end

    def test_matches_capture_heavy_patterns_like_ruby
      assert_match_data_like_ruby('(()|())*a', ['', 'a', 'aa', 'ba', 'xa', 'ab', 'aaa'])
      assert_match_data_like_ruby('(a|aa)*b', ['', 'b', 'ab', 'aab', 'aaab', 'aaa', 'baab'])
      assert_match_data_like_ruby('((ab)|a)*b', ['', 'b', 'ab', 'aab', 'aaab', 'abaab', 'aaa'])
      assert_match_data_like_ruby('(a?)*b', ['', 'b', 'ab', 'aab', 'aaab', 'c'])
      assert_match_data_like_ruby('((a*)*)b', ['', 'b', 'ab', 'aaab', 'a', 'ba'])
      assert_match_data_like_ruby('((a|)*)b', ['', 'b', 'ab', 'aaab', 'a', 'ba'])
      assert_match_data_like_ruby('((?:)|a)*b', ['', 'b', 'ab', 'aaab', 'a', 'ba'])
      assert_match_data_like_ruby('(x|(y|z))*a', ['', 'a', 'xa', 'yza', 'xyza', 'xyz', 'b'])
      assert_match_data_like_ruby('((..)|(.))*a', ['', 'a', 'ba', 'bca', 'bcda', 'xyz', 'za'])
      assert_match_data_like_ruby('((a)|(ab))*c', ['', 'c', 'ac', 'abc', 'abac', 'abababc', 'ab'])
    end

    def test_capture_reference_for_empty_group_alternation
      assert_match_data_like_ruby('(()|())*a', 'a')
      match = NarakuRuby::DFA.match('(()|())*a', 'a')
      refute_nil match
      assert_equal '', match[1]
      assert_equal '', match[2]
    end

    def test_extracts_capture_values_like_ruby
      assert_match_data_like_ruby('(a)(b)', 'zabz')
      assert_match_data_like_ruby('(a)?b', 'b')
      assert_match_data_like_ruby('(a+)(b+)', 'xaaabbbz')
      assert_match_data_like_ruby('((ab)|a)*b', 'aaab')
      assert_match_data_like_ruby('(()|())*a', 'aaa')
    end

    def test_returns_nil_when_no_match
      dfa_program = NarakuRuby::DFA.compile('(a)(b)')
      dfa_match, dfa_match_p = assert_match_consistency(dfa_program, 'zzz', '(a)(b)')
      assert_nil dfa_match
      assert_equal false, dfa_match_p
    end

    def test_bulk_compatibility_on_supported_subset
      patterns = [
        'a', 'ab', 'a|b', '(a|b)c', '(a|)b', '()a', '(ab)*c', '(ab)+c', '(ab)?c',
        '(ab){0,2}c', '(ab){1,3}c', '(a|ab)*b', '((a|b)*)c', '[ab]+c', '[^a]*b',
        '\\d+\\w?', '\\w*\\d', '.a', '^a$', '\\Afoo\\Z', '\\bcat\\b', '\\Bcat\\B',
        '(x|(y|z))*a', '((ab)|a)*b', '(()|())*a'
      ]
      samples = [
        '', 'a', 'b', 'c', 'ab', 'ba', 'abc', 'aab', 'aaab', 'bca', 'x',
        '1', '_', 'Ω', 'あ', 'aΩ', 'Ωa', "\n", "a\n", "\na", 'cat', 'xcaty'
      ]

      patterns.each do |pattern|
        assert_match_data_like_ruby(pattern, samples)
      end
    end

    def test_bulk_match_data_compatibility_on_capture_patterns
      patterns = [
        '(a)(b)', '(a)?b', '(a+)(b+)', '((ab)|a)*b', '(()|())*a', '((a|)*)b',
        '(a|aa)*b', '((a)|(ab))*c', '((..)|(.))*a', '(x|(y|z))*a',
        '(a+?)(b+)', '(a*)(b*)c', '((a)?(b)?)(c)', '^((a|)*)$', '\\A((ab)*)\\z',
        '(\\w+)-(\\d+)', '([a-z]*)([A-Z]*)', '(Ω*)(あ*)', '(a{0,2})(b{1,3})'
      ]
      samples = [
        '', 'a', 'b', 'ab', 'aaabbb', 'aaab', 'abc', 'ababab', 'c', 'cat',
        'xcaty', 'Ω', 'あ', 'ΩΩああ', 'foo-123', 'FOO', 'abcDEF', "a\n", "\n", 'aaa', 'bbb'
      ]

      patterns.each do |pattern|
        samples.each do |sample|
          assert_match_data_like_ruby(pattern, sample)
        end
      end
    end

    def test_randomized_compatibility_corpus
      skip 'set NARAKU_DFA_RANDOM_COMPAT=1 to enable randomized compatibility corpus' unless ENV['NARAKU_DFA_RANDOM_COMPAT'] == '1'

      rng = Random.new(19_970_415)
      patterns = build_random_patterns(rng, count: 100, depth: 3)
      samples = [
        '', 'a', 'b', 'c', 'ab', 'ba', 'abc', 'aab', 'aaab', 'bbb',
        'Ω', 'あ', 'Ωあ', 'aΩ', 'Ωa', "\n", "a\n", "\na", '1', '_'
      ]

      patterns.each do |pattern, parser_options, regexp_options|
        assert_match_data_like_ruby(
          pattern,
          samples,
          regexp_options:,
          parser_options:
        )
      end
    end

    def test_rejects_back_reference
      assert_unsupported('(a)\\1')
    end

    def test_rejects_subexpression_call
      assert_unsupported('(?<a>x)\\g<a>')
    end

    def test_rejects_lookaround
      assert_unsupported('(?=a)b')
      assert_unsupported('(?<!a)b')
    end

    def test_rejects_conditional_absence_and_atomic
      assert_unsupported('(a)?(?(1)b|c)')
      assert_unsupported('(?~a)')
      assert_unsupported('(?>a)')
    end

    def test_rejects_possessive_quantifier
      assert_unsupported('a*+')
    end

    def test_rejects_strict_char_class_with_multi_character_expansion
      assert_unsupported('[ß]', parser_options: { is_ignore_case: true, fold_flags: [:full], char_class_is_strict: true })
    end

    def test_handles_ascii_word_boundary_assertions
      patterns = ['(?a)\b', '(?a)\B', '(?u)\b', '(?u)\B']
      samples = ['', 'a', 'Ω', 'aΩ', 'Ωa']

      patterns.each do |pattern|
        assert_match_data_like_ruby(pattern, samples)
      end
    end

    def test_non_ruby_group_option_v_enables_strict_char_class
      assert_match '(?i:[[a-zA-Z]&&[^A]])', 'A'
      refute_match '(?iv:[[a-zA-Z]&&[^A]])', 'A'
    end

    def test_non_ruby_group_option_f_enables_full_case_fold
      refute_match '(?i:[ß])', 'ss'
      assert_match '(?iF:[ß])', 'ss'
    end

    def test_non_ruby_group_option_i_forces_ascii_only_fold
      assert_match '(?i:[ß])', 'ẞ'
      refute_match '(?I:[ß])', 'ẞ'
    end

    def test_non_ruby_group_option_a_sets_ascii_only_fold_flags
      assert_match '(?i:[Ä])', 'ä'
      refute_match '(?iA:[Ä])', 'ä'
      assert_match '(?iA:[A])', 'a'
      refute_match '(?iA:[A])', 'Ä'
    end

    def test_non_ruby_group_option_t_enables_turkish_azeri_fold
      assert_match '(?i:[I])', 'i'
      refute_match '(?i:[I])', 'ı'
      refute_match '(?iT:[I])', 'i'
      assert_match '(?iT:[I])', 'ı'
    end

    def test_non_ruby_group_option_s_sets_simple_fold_flags
      assert_match '(?iF:[ß])', 'ss'
      refute_match '(?iS:[ß])', 'ss'
      refute_match '(?iFS:[ß])', 'ss'
    end

    def test_full_dfa_match_is_consistent_for_simple_non_capture_patterns
      patterns = ['a+b', '(a|a)+b']
      samples = ['', 'b', 'ab', 'aaab', 'aaaaac', 'zaaabz']

      patterns.each do |pattern|
        regular = NarakuRuby::DFA.compile(pattern)
        full_dfa = NarakuRuby::DFA.compile(pattern, full_dfa: true)
        full_dfa_eval = NarakuRuby::DFA.compile(pattern, full_dfa: true, eval: true)
        samples.each do |sample|
          assert_equal regular.match?(sample), full_dfa.match?(sample), "pattern=#{pattern.inspect} sample=#{sample.inspect}"
          assert_equal regular.match?(sample), full_dfa_eval.match?(sample), "pattern=#{pattern.inspect} sample=#{sample.inspect} (full_dfa+eval)"
        end
      end
    end

    def test_start_pos_is_codepoint_based_for_match_and_match_predicate
      ascii_program = NarakuRuby::DFA.compile('ab')
      ascii_match, ascii_match_p = assert_match_consistency(ascii_program, 'zzab', 'ab', pos: 2)
      refute_nil ascii_match
      assert_equal true, ascii_match_p
      assert_equal 'ab', ascii_match[0]

      ascii_nomatch, ascii_nomatch_p = assert_match_consistency(ascii_program, 'zzab', 'ab', pos: 3)
      assert_nil ascii_nomatch
      assert_equal false, ascii_nomatch_p

      utf8_program = NarakuRuby::DFA.compile('b')
      utf8_match, utf8_match_p = assert_match_consistency(utf8_program, 'aΩb', 'b', pos: 2)
      refute_nil utf8_match
      assert_equal true, utf8_match_p
      assert_equal 'b', utf8_match[0]

      utf8_nomatch, utf8_nomatch_p = assert_match_consistency(utf8_program, 'aΩb', 'b', pos: 3)
      assert_nil utf8_nomatch
      assert_equal false, utf8_nomatch_p
    end

    def test_raises_for_invalid_utf8_input
      program = NarakuRuby::DFA.compile('.+')
      invalid = "\xE3\x81".dup.force_encoding(::Encoding::UTF_8)

      error = assert_raises(ArgumentError) { program.match?(invalid) }
      assert_includes error.message, 'invalid byte sequence in UTF-8'

      error = assert_raises(ArgumentError) { program.match(invalid) }
      assert_includes error.message, 'invalid byte sequence in UTF-8'
    end

    private

    def assert_match_data_like_ruby(pattern, string, regexp_options: 0, parser_options: {})
      string = [string] if string.is_a?(String)

      ruby_regexp = Regexp.new(pattern, regexp_options)
      dfa_program = NarakuRuby::DFA.compile(pattern, **parser_options)
      string.each do |s|
        ruby_match = ruby_regexp.match(s)
        dfa_match, dfa_match_p = assert_match_consistency(dfa_program, s, pattern)

        if ruby_match.nil?
          assert_nil dfa_match, "pattern=#{pattern.inspect} string=#{s.inspect}"
          assert_equal false, dfa_match_p, "pattern=#{pattern.inspect} string=#{s.inspect}"
          next
        end

        refute_nil dfa_match, "pattern=#{pattern.inspect} string=#{s.inspect}"
        assert_equal true, dfa_match_p, "pattern=#{pattern.inspect} string=#{s.inspect}"
        assert_equal ruby_match[0], dfa_match[0], "pattern=#{pattern.inspect} string=#{s.inspect}"
        assert_equal ruby_match.captures, dfa_match.captures, "pattern=#{pattern.inspect} string=#{s.inspect}"

        ruby_match_pos = ruby_match.length.times.map { |i| ruby_match.begin(i)..ruby_match.end(i) }
        dfa_match_pos = dfa_match.length.times.map { |i| dfa_match.begin(i)..dfa_match.end(i) }

        assert_equal ruby_match_pos, dfa_match_pos, "pattern=#{pattern.inspect} string=#{s.inspect}"
      end
    end

    def assert_unsupported(pattern, parser_options: {})
      assert_raises(NarakuRuby::DFA::CompileError) do
        NarakuRuby::DFA.compile(pattern, **parser_options)
      end
    end

    def assert_match(pattern, string, parser_options: {})
      dfa_program = NarakuRuby::DFA.compile(pattern, postprocess: true, **parser_options)
      dfa_match, dfa_match_p = assert_match_consistency(dfa_program, string, pattern)
      refute_nil dfa_match, "expected pattern #{pattern.inspect} to match string #{string.inspect}"
      assert dfa_match_p, "expected pattern #{pattern.inspect} to match string #{string.inspect}"
    end

    def refute_match(pattern, string, parser_options: {})
      dfa_program = NarakuRuby::DFA.compile(pattern, postprocess: true, **parser_options)
      dfa_match, dfa_match_p = assert_match_consistency(dfa_program, string, pattern)
      assert_nil dfa_match, "expected pattern #{pattern.inspect} to not match string #{string.inspect}"
      refute dfa_match_p, "expected pattern #{pattern.inspect} to not match string #{string.inspect}"
    end

    def assert_match_consistency(dfa_program, string, pattern, pos: 0)
      dfa_match = dfa_program.match(string, pos)
      dfa_match_p = dfa_program.match?(string, pos)
      assert_equal !dfa_match.nil?, dfa_match_p,
                   "pattern=#{pattern.inspect} string=#{string.inspect} pos=#{pos.inspect} must be consistent between match and match?"
      [dfa_match, dfa_match_p]
    end

    def build_random_patterns(rng, count:, depth:)
      patterns = []
      while patterns.length < count
        parser_options = rng.rand < 0.25 ? { is_ignore_case: true, fold_flags: [:full] } : {}
        regexp_options = parser_options[:is_ignore_case] ? Regexp::IGNORECASE : 0
        pattern = random_expr(rng, depth)

        begin
          Regexp.new(pattern, regexp_options)
          NarakuRuby::DFA.compile(pattern, postprocess: true, **parser_options)
        rescue RegexpError, NarakuRuby::DFA::CompileError, NarakuRuby::MRubyBridgeError
          next
        end

        patterns << [pattern, parser_options, regexp_options]
      end
      patterns
    end

    def random_expr(rng, depth)
      n = rng.rand(1..3)
      parts = Array.new(n) { random_quantified(rng, depth) }
      rng.rand < 0.35 ? "#{parts.join}|#{random_quantified(rng, depth)}" : parts.join
    end

    def random_quantified(rng, depth)
      atom = random_atom(rng, depth)
      quantifier = ['', '*', '+', '?', '{0,2}', '{1,3}', '*?', '+?', '{0,2}?', '{1,3}?'].sample(random: rng)
      "#{atom}#{quantifier}"
    end

    def random_atom(rng, depth)
      tokens = ['a', 'b', 'c', 'Ω', 'あ', '\\d', '\\w', '[ab]', '[^a]', '.']
      base = tokens.sample(random: rng)
      return base if depth <= 0 || rng.rand < 0.62

      [
        -> { "(#{random_plain_expr(rng, depth - 1)})" },
        -> { "(?<foo>#{random_plain_expr(rng, depth - 1)})" },
        -> { "(?:#{random_plain_expr(rng, depth - 1)})" },
        -> { "(?i:#{random_plain_expr(rng, depth - 1)})" },
        -> { "(?m:#{random_plain_expr(rng, depth - 1)})" },
        -> { "(#{random_plain_expr(rng, depth - 1)}|#{random_plain_expr(rng, depth - 1)})" },
        -> { "(#{base})" },
        -> { "(#{base}|)" },
      ].sample(random: rng).call
    end

    def random_plain_expr(rng, depth)
      n = rng.rand(1..2)
      atoms = Array.new(n) do
        token_pool = ['a', 'b', 'c', 'Ω', 'あ', '\\d', '\\w', '[ab]', '[^a]', '.']
        token = token_pool.sample(random: rng)
        if depth.positive? && rng.rand < 0.25
          "(#{token}|)"
        else
          token
        end
      end
      atoms.join
    end
  end
end
