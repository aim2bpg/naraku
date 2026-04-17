# frozen_string_literal: true

require_relative '../test_helper'

module NarakuRuby
  class EncodingTest < Minitest::Test
    def setup
      NarakuRuby.clear_cache!
    end

    def test_case_fold_handles_multiple_characters
      input = 'Ab'.bytes
      assert_equal 'ab'.bytes, NarakuRuby::Encoding.case_fold(input)
    end

    def test_case_fold_with_full_flag
      assert_equal 'ss'.bytes, NarakuRuby::Encoding.case_fold('ß'.bytes, :full)
    end

    def test_case_fold_with_turkish_azeri_flag
      assert_equal 'ı'.bytes, NarakuRuby::Encoding.case_fold('I'.bytes, :turkish_azeri)
    end

    def test_case_fold_with_ascii_only_flag
      assert_equal 'Äa'.bytes, NarakuRuby::Encoding.case_fold('ÄA'.bytes, :ascii_only)
    end

    def test_case_fold_keeps_characters_without_fold
      input = 'abc'.bytes
      assert_equal 'abc'.bytes, NarakuRuby::Encoding.case_fold(input)
    end

    def test_case_fold_rejects_non_utf8_bytes
      assert_raises(ArgumentError) do
        NarakuRuby::Encoding.case_fold([0xFF])
      end
    end

    def test_expand_case_unfold_handles_empty_input
      assert_equal [0, [{ type: :match }]], NarakuRuby::Encoding.expand_case_unfold([])
    end

    def test_expand_case_unfold_with_full_flag_includes_eszett
      root, nodes = NarakuRuby::Encoding.expand_case_unfold('ss'.bytes, :full)
      unfolded_strings = unfold_strings(root, nodes)

      assert_includes unfolded_strings, 'ss'
      assert_includes unfolded_strings, 'ß'
      assert_includes unfolded_strings, 'ẞ'
    end

    def test_expand_case_unfold_with_turkish_azeri_flag
      root, nodes = NarakuRuby::Encoding.expand_case_unfold('i'.bytes, :turkish_azeri)
      unfolded_strings = unfold_strings(root, nodes)

      assert_includes unfolded_strings, 'i'
      assert_includes unfolded_strings, 'İ'
    end

    def test_expand_case_unfold_rejects_non_utf8_bytes
      assert_raises(ArgumentError) do
        NarakuRuby::Encoding.expand_case_unfold([0xFF])
      end
    end

    private

    def unfold_strings(root, nodes)
      unfold_code_paths(root, nodes).map { |codes| codes.pack('U*') }
    end

    def unfold_code_paths(index, nodes)
      node = nodes[index]
      case node[:type]
      when :match
        [[]]
      when :code
        unfold_code_paths(node[:next], nodes).map { |suffix| [node[:code], *suffix] }
      when :alt
        node[:children].flat_map { |child| unfold_code_paths(child, nodes) }
      else
        raise "unknown node type: #{node[:type].inspect}"
      end
    end
  end
end
