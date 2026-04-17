# frozen_string_literal: true

require_relative '../test_helper'

class NarakuRubyCharClassBuilderTest < Minitest::Test
  def test_build_char_class_with_intersection
    node = NarakuRuby.parse('[a-z&&[d-f]]')[:node]
    char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

    assert_equal false, char_class.include?('a'.ord)
    assert_equal true, char_class.include?('e'.ord)
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

  def test_non_strict_ignore_case_negative_does_not_add_multi_char_expansion
    node = NarakuRuby.parse('[^ß]', is_ignore_case: true, fold_flags: [:full], char_class_is_strict: false)[:node]
    _char_class, expanded_strings = NarakuRuby::CharClassBuilder.new(node).build

    assert_equal Set.new, expanded_strings
  end

  def test_strict_ignore_case_negative_after_multi_char_expansion_raises_error_with_span
    node = NarakuRuby.parse('[^ß]', is_ignore_case: true, fold_flags: [:full], char_class_is_strict: true)[:node]

    error = assert_raises(NarakuRuby::CharClassBuilderError) do
      NarakuRuby::CharClassBuilder.new(node).build
    end
    assert_equal node[:span_offset], error.offset
    assert_equal node[:span_length], error.length
  end
end
