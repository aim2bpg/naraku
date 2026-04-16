require_relative 'test_helper'

class TestParse < Minitest::Test
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
