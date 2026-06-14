# frozen_string_literal: true

require_relative '../test_helper'

module NarakuRuby
  class CharClassBuilderTest < Minitest::Test
    def test_build_raises_argument_error_for_unsupported_top_node
      node = NarakuRuby.parse('a')[:node]

      error = assert_raises(ArgumentError) do
        NarakuRuby::CharClassBuilder.new(node).build
      end
      assert_equal 'node must be :char_class, :char_type, or :char_prop', error.message
    end

    def test_build_top_level_char_type_node
      node = NarakuRuby.parse('\\w')[:node]
      char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal true, char_class.include?('a'.ord)
      assert_equal true, char_class.include?('0'.ord)
      assert_equal false, char_class.include?('-'.ord)
      assert_equal Set.new, expanded_strings
    end

    def test_build_top_level_char_prop_node
      node = NarakuRuby.parse('\\p{Lu}')[:node]
      char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal true, char_class.include?('A'.ord)
      assert_equal false, char_class.include?('a'.ord)
      assert_equal Set.new, expanded_strings
    end

    def test_build_char_class_with_intersection
      node = NarakuRuby.parse('[a-z&&[d-f]]')[:node]
      char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal false, char_class.include?('a'.ord)
      assert_equal true, char_class.include?('e'.ord)
      assert_equal Set.new, expanded_strings
    end

    def test_build_char_class_with_nested_negation_and_intersection
      node = NarakuRuby.parse('[[^a]&&[ab]]')[:node]
      char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal false, char_class.include?('a'.ord)
      assert_equal true, char_class.include?('b'.ord)
      assert_equal false, char_class.include?('c'.ord)
      assert_equal Set.new, expanded_strings
    end

    def test_build_char_type_and_char_prop_items
      node = NarakuRuby.parse('[\\d\\p{Lu}]')[:node]
      char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal true, char_class.include?('0'.ord)
      assert_equal true, char_class.include?('A'.ord)
      assert_equal false, char_class.include?('a'.ord)
      assert_equal Set.new, expanded_strings
    end

    def test_build_posix_char_class_items
      node = NarakuRuby.parse('[[:digit:][:^digit:]]')[:node]
      char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      # union of digit and non-digit -> all code points
      assert_equal true, char_class.include?('0'.ord)
      assert_equal true, char_class.include?('A'.ord)
      assert_equal true, char_class.include?(0x3042)
      assert_equal Set.new, expanded_strings
    end

    def test_strict_ignore_case_intersection_is_fold_aware
      node = NarakuRuby.parse('[A&&a]', is_ignore_case: true, char_class_is_strict: true)[:node]
      char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal true, char_class.include?('A'.ord)
      assert_equal true, char_class.include?('a'.ord)
      assert_equal Set.new, expanded_strings
    end

    def test_non_strict_ignore_case_intersection_is_not_fold_aware_midway
      node = NarakuRuby.parse('[A&&a]', is_ignore_case: true, char_class_is_strict: false)[:node]
      char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal false, char_class.include?('A'.ord)
      assert_equal false, char_class.include?('a'.ord)
      assert_equal Set.new, expanded_strings
    end

    def test_non_strict_ignore_case_full_expansion_is_kept_when_ascii_filter_allows
      node = NarakuRuby.parse('[sß]', is_ignore_case: true, fold_flags: [:full], char_class_is_strict: false)[:node]
      _char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal Set['ss'], expanded_strings
    end

    def test_non_strict_ignore_case_full_expansion_is_kept_for_explicit_sharp_s
      node = NarakuRuby.parse('[ß]', is_ignore_case: true, fold_flags: [:full], char_class_is_strict: false)[:node]
      char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal true, char_class.include?(0x00DF) # LATIN SMALL LETTER SHARP S
      assert_equal true, char_class.include?(0x1E9E) # LATIN CAPITAL LETTER SHARP S
      assert_equal Set['ss'], expanded_strings
    end

    def test_non_strict_ignore_case_negative_does_not_add_multi_char_expansion
      node = NarakuRuby.parse('[^ß]', is_ignore_case: true, fold_flags: [:full], char_class_is_strict: false)[:node]
      _char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal Set.new, expanded_strings
    end

    def test_non_strict_ignore_case_negated_a_excludes_a_and_a_upper
      node = NarakuRuby.parse('[^a]', is_ignore_case: true, char_class_is_strict: false)[:node]
      char_class, _expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal false, char_class.include?('a'.ord)
      assert_equal false, char_class.include?('A'.ord)
      assert_equal true, char_class.include?('b'.ord)
    end

    def test_non_strict_ignore_case_intersection_with_negated_branch_matches_a_upper
      node = NarakuRuby.parse('[[a-zA-Z]&&[^A]]', is_ignore_case: true, char_class_is_strict: false)[:node]
      char_class, _expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal true, char_class.include?('A'.ord)
    end

    def test_strict_ignore_case_intersection_with_negated_branch_excludes_a_upper
      node = NarakuRuby.parse('[[a-zA-Z]&&[^A]]', is_ignore_case: true, char_class_is_strict: true)[:node]
      char_class, _expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal false, char_class.include?('A'.ord)
    end

    def test_non_strict_ignore_case_word_does_not_include_long_s_or_kelvin
      node = NarakuRuby.parse('[\\w]', is_ignore_case: true, char_class_is_strict: false)[:node]
      char_class, _expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal true, char_class.include?('a'.ord)
      assert_equal true, char_class.include?('K'.ord)
      assert_equal false, char_class.include?(0x017F) # LATIN SMALL LETTER LONG S
      assert_equal false, char_class.include?(0x212A) # KELVIN SIGN
    end

    def test_non_strict_ignore_case_word_with_explicit_s_includes_long_s_only
      node = NarakuRuby.parse('[\\ws]', is_ignore_case: true, char_class_is_strict: false)[:node]
      char_class, _expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

      assert_equal true, char_class.include?('s'.ord)
      assert_equal true, char_class.include?(0x017F) # LATIN SMALL LETTER LONG S
      assert_equal false, char_class.include?(0x212A) # KELVIN SIGN
    end

    def test_strict_ignore_case_negative_after_multi_char_expansion_raises_error_with_span
      node = NarakuRuby.parse('[^ß]', is_ignore_case: true, fold_flags: [:full], char_class_is_strict: true)[:node]

      error = assert_raises(NarakuRuby::CharClassBuildError) do
        NarakuRuby::CharClassBuilder.new(node).build
      end
      assert_equal node[:span_offset], error.offset
      assert_equal node[:span_length], error.length
    end

    def test_strict_ignore_case_nested_negative_multi_char_expansion_error_has_nested_span
      node = NarakuRuby.parse('[[^ß]]', is_ignore_case: true, fold_flags: [:full], char_class_is_strict: true)[:node]
      nested_item = node[:unions][0][:items][0]

      error = assert_raises(NarakuRuby::CharClassBuildError) do
        NarakuRuby::CharClassBuilder.new(node).build
      end
      assert_equal nested_item[:span_offset], error.offset
      assert_equal nested_item[:span_length], error.length
    end

    def test_unsupported_item_type_raises_error_with_item_span
      node = NarakuRuby.parse('[a]')[:node]
      item = node[:unions][0][:items][0]
      item[:type] = :unknown_item

      error = assert_raises(NarakuRuby::CharClassBuildError) do
        NarakuRuby::CharClassBuilder.new(node).build
      end
      assert_equal item[:span_offset], error.offset
      assert_equal item[:span_length], error.length
    end

    def test_build_error_with_nil_offset_has_no_span_in_message
      err = NarakuRuby::CharClassBuildError.new('test message', offset: nil, length: 0)
      assert_equal 'test message', err.message
      assert_nil err.offset
    end

    def test_build_ignore_case_posix_class_skips_ascii_tracking
      node = NarakuRuby.parse('[[:digit:]]', is_ignore_case: true)[:node]
      char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build
      assert_equal true, char_class.include?('0'.ord)
      assert_equal Set.new, expanded_strings
    end

    def test_build_ignore_case_char_prop_skips_ascii_tracking
      node = NarakuRuby.parse('[\\p{Lu}]', is_ignore_case: true)[:node]
      char_class, = NarakuRuby::CharClassBuilder.new(node).build
      assert_equal true, char_class.include?('A'.ord)
      assert_equal true, char_class.include?('a'.ord)
    end
  end
end
