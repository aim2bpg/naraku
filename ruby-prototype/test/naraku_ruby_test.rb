# frozen_string_literal: true

require_relative 'test_helper'

class NarakuRubyTest < Minitest::Test
  def setup
    NarakuRuby.clear_cache!
  end

  def test_normalizes_node_type_values_to_symbols
    result = NarakuRuby.parse('(?=a)\w*[[:digit:]]')
    node = result[:node]

    refute_nil node
    assert_instance_of Symbol, node[:type]
    assert_equal :concat, node[:type]

    assertion = first_node(node) { |n| n[:type] == :assertion }
    quantifier = first_node(node) { |n| n[:type] == :quantifier }
    char_type = first_node(node) { |n| n[:type] == :char_type }
    posix_item = first_node(node) { |n| n[:type] == :posix_char_class }

    assert_equal :positive_lookahead, assertion[:assertion_type]
    assert_equal :greedy, quantifier[:quantifier_type]
    assert_equal :word, char_type[:char_type]
    assert_equal :digit, posix_item[:posix_char_class]
  end

  def test_keeps_non_enum_values_as_is
    result = NarakuRuby.parse('(?<foo>a)\k<foo>')
    group = first_node(result[:node]) { |n| n[:type] == :capture }
    back_ref = first_node(result[:node]) { |n| n[:type] == :back_ref }

    assert_instance_of String, group[:name]
    assert_instance_of String, back_ref[:name]
    assert_equal 'foo', group[:name]
    assert_equal 'foo', back_ref[:name]
  end

  def test_includes_parser_info_without_postprocess
    result = NarakuRuby.parse('(a)')
    parser_info = result[:parser_info]

    assert_equal 1, parser_info[:num_capture_groups]
    assert_equal false, parser_info[:has_named_captures]
    refute parser_info.key?(:capture_entries)
    refute parser_info.key?(:capture_names_map)
  end

  def test_includes_postprocess_parser_info
    result = NarakuRuby.parse('(?<foo>a)(?<foo>b)\k<foo>', postprocess: true)
    parser_info = result[:parser_info]

    assert_equal 2, parser_info[:num_capture_groups]
    assert_equal true, parser_info[:has_named_captures]
    assert_equal 1, parser_info[:capture_entries].length
    assert_equal [1, 2], parser_info[:capture_entries][0][:capture_nums]
    assert_equal 0, parser_info[:capture_names_map][:foo]
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

  def test_iterate_case_fold_with_turkish_azeri_flag
    items = NarakuRuby.iterate_case_fold(:turkish_azeri)
    item = items.find { |entry| entry[:code] == 0x49 }

    refute_nil item
    assert_equal [0x131], item[:folded_codes]
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

  private

  def first_node(node, &)
    find_nodes(node, &).first
  end

  def find_nodes(node, &block)
    nodes = []
    if node.is_a?(Hash)
      nodes << node if block.call(node)
      node.each_value do |value|
        if value.is_a?(Hash)
          nodes.concat(find_nodes(value, &block))
        elsif value.is_a?(Array)
          value.each { |child| nodes.concat(find_nodes(child, &block)) }
        end
      end
    end
    nodes
  end
end
