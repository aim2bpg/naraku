# frozen_string_literal: true

require_relative '../test_helper'

module NarakuRuby
  class CharClassTest < Minitest::Test
    def test_initialization_normalizes_ranges
      char_class = NarakuRuby::CharClass.new([10..12, 1..3, 3..10, 20..20])

      assert_equal [1..12, 20..20], char_class.ranges
    end

    def test_include_works_with_binary_search
      char_class = NarakuRuby::CharClass.new([1..3, 10..12, 20..22])

      assert_equal true, char_class.include?(1)
      assert_equal true, char_class.include?(11)
      assert_equal true, char_class.include?(22)
      assert_equal false, char_class.include?(4)
      assert_equal false, char_class.include?(19)
    end

    def test_add_code_and_range
      char_class = NarakuRuby::CharClass.new([1..3, 7..8])
      char_class.add(4)
      char_class.add(5..6)

      assert_equal [1..8], char_class.ranges
    end

    def test_add_exclusive_range
      char_class = NarakuRuby::CharClass.new
      char_class.add(5...8)

      assert_equal [5..7], char_class.ranges
    end

    def test_delete_code_splits_range
      char_class = NarakuRuby::CharClass.new([1..5])
      char_class.delete(3)

      assert_equal [1..2, 4..5], char_class.ranges
    end

    def test_delete_range_across_multiple_ranges
      char_class = NarakuRuby::CharClass.new([1..5, 10..15, 20..22])
      char_class.delete(4..20)

      assert_equal [1..3, 21..22], char_class.ranges
    end

    def test_union_returns_new_char_class
      left = NarakuRuby::CharClass.new([1..3, 10..12])
      right = NarakuRuby::CharClass.new([3..5, 20..21])

      union = left.union(right)

      assert_equal [1..5, 10..12, 20..21], union.ranges
      assert_equal [1..3, 10..12], left.ranges
      assert_equal [3..5, 20..21], right.ranges
    end

    def test_intersect_returns_common_ranges
      left = NarakuRuby::CharClass.new([1..6, 10..15, 20..25])
      right = NarakuRuby::CharClass.new([3..12, 14..20, 30..31])

      intersection = left.intersect(right)

      assert_equal [3..6, 10..12, 14..15, 20..20], intersection.ranges
    end

    def test_negate_in_boundaries
      char_class = NarakuRuby::CharClass.new([2..4, 7..8])
      negated = char_class.negate(0, 10)

      assert_equal [0..1, 5..6, 9..10], negated.ranges
    end

    def test_negate_with_no_ranges
      char_class = NarakuRuby::CharClass.new
      negated = char_class.negate(3, 5)

      assert_equal [3..5], negated.ranges
    end

    def test_rejects_invalid_values
      char_class = NarakuRuby::CharClass.new

      assert_raises(ArgumentError) { char_class.add('a') }
      assert_raises(ArgumentError) { char_class.add((-1)..5) }
      assert_raises(ArgumentError) { char_class.include?(-1) }
      assert_raises(ArgumentError) { char_class.negate(10, 1) }
    end

    def test_case_fold_unfolds_single_codepoint_equivalence
      char_class = NarakuRuby::CharClass.new([0x61..0x61]) # 'a'
      folded_class, expanded_strings = char_class.case_fold

      assert_equal true, folded_class.include?(0x61) # 'a'
      assert_equal true, folded_class.include?(0x41) # 'A'
      assert_equal Set.new, expanded_strings
    end

    def test_case_fold_full_returns_expanded_strings
      char_class = NarakuRuby::CharClass.new([0xDF..0xDF]) # 'ß'
      folded_class, expanded_strings = char_class.case_fold(:full)

      assert_equal true, folded_class.include?(0xDF) # 'ß'
      assert_equal true, folded_class.include?(0x1E9E) # 'ẞ'
      assert_equal Set['ss'], expanded_strings
    end
  end
end
