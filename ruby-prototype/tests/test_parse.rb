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
