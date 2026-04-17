# frozen_string_literal: true

require_relative 'test_helper'

class TestEncoding < Minitest::Test
  def setup
    NarakuRuby.clear_cache!
  end

  def test_case_fold_uses_utf8
    assert_equal [0x61], NarakuRuby.case_fold(0x41)
  end

  def test_expand_case_unfold_uses_utf8
    result = NarakuRuby.expand_case_unfold([0x61])

    refute_nil result
    assert(result.any? { |item| item[:unfolded_code] == 0x41 })
  end

  def test_iterate_case_fold_uses_utf8
    items = NarakuRuby.iterate_case_fold
    assert(items.any? { |item| item[:code] == 0x41 && item[:folded_codes] == [0x61] })
  end

  def test_cprop_code_range_uses_utf8
    assert_equal [0x00..0x7F], NarakuRuby.cprop_code_range('ASCII')
  end

  def test_encoding_bridge_caches_by_request
    calls = 0
    Open3.singleton_class.class_eval do
      alias_method :__naraku_original_capture3, :capture3
      define_method(:capture3) do |*args, **kwargs|
        calls += 1
        __naraku_original_capture3(*args, **kwargs)
      end
    end

    begin
      NarakuRuby.case_fold(0x41)
      NarakuRuby.case_fold(0x41)
      assert_equal 1, calls
    ensure
      Open3.singleton_class.class_eval do
        remove_method :capture3
        alias_method :capture3, :__naraku_original_capture3
        remove_method :__naraku_original_capture3
      end
    end
  end
end

class TestNarakuRubyEncoding < Minitest::Test
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
    assert_equal [0, [:match]], NarakuRuby::Encoding.expand_case_unfold([])
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
    unfold_code_paths(root, nodes).map { |codes| codes.pack('U*') }.uniq
  end

  def unfold_code_paths(index, nodes)
    node = nodes[index]
    case node
    when :match
      [[]]
    when Hash
      case node[:type]
      when :code
        unfold_code_paths(node[:next], nodes).map { |suffix| [node[:code], *suffix] }
      when :alt
        node[:children].flat_map { |child| unfold_code_paths(child, nodes) }
      else
        raise "unknown node type: #{node[:type].inspect}"
      end
    else
      raise "unknown node: #{node.inspect}"
    end
  end
end
