require_relative 'test_helper'

class TestPreprocess < Minitest::Test
  def test_rewrite_named_and_unnamed_groups
    resolved = preprocess_pattern('(a)(?<foo>b)(c)(?<bar>d)')
    group_nums = nodes_of_type(resolved[:node], :group).map { |node| node[:group_num] }
    assert_equal [0, 1, 0, 2], group_nums
  end

  def test_no_named_group_keeps_original_group_nums
    resolved = preprocess_pattern('(a)(b)')
    group_nums = nodes_of_type(resolved[:node], :group).map { |node| node[:group_num] }
    assert_equal [1, 2], group_nums
  end

  def test_named_mode_resolves_seen_named_groups_and_warns_for_later_groups
    resolved = preprocess_pattern('(?<foo>a)\k<foo>(?<foo>b)')
    back_ref = first_node_of_type(resolved[:node], :back_ref)

    assert_equal [1], back_ref[:resolved_group_nums]
    assert_equal [1, 2], resolved[:group_nums_by_name]['foo']
    assert_equal 1, resolved[:warnings].length
    assert_equal :named_back_ref_excludes_later_groups, resolved[:warnings][0][:type]
  end

  def test_named_mode_rejects_numeric_back_ref
    assert_preprocess_error('(?<foo>a)\k<1>', :numeric_back_ref_with_named_groups)
  end

  def test_named_mode_reports_forward_named_back_ref
    assert_preprocess_error('\k<foo>(?<foo>a)', :forward_named_back_ref)
  end

  def test_named_mode_reports_unknown_named_back_ref
    assert_preprocess_error('(?<foo>a)\k<bar>', :undefined_named_back_ref)
  end

  def test_named_mode_resolves_conditional_group_like_back_ref
    resolved = preprocess_pattern('(?<foo>a)(?(<foo>)b)')
    conditional = first_node_of_type(resolved[:node], :conditional)
    assert_equal [1], conditional[:resolved_group_nums]
  end

  def test_named_mode_rejects_numeric_conditional_group
    assert_preprocess_error('(?<foo>a)(?(1)b)', :numeric_back_ref_with_named_groups)
  end

  def test_non_named_mode_resolves_numeric_back_ref
    resolved = preprocess_pattern('(a)(b)\k<2>')
    back_ref = first_node_of_type(resolved[:node], :back_ref)
    assert_equal [2], back_ref[:resolved_group_nums]
  end

  def test_non_named_mode_rejects_named_back_ref
    assert_preprocess_error('(a)\k<foo>', :named_back_ref_without_named_groups)
  end

  def test_non_named_mode_rejects_out_of_range_numeric_back_ref
    assert_preprocess_error('(a)\k<2>', :back_ref_group_num_out_of_range)
  end

  def test_subexp_call_resolves_with_exactly_one_named_group
    resolved = preprocess_pattern('\g<foo>(?<foo>a)')
    call = first_node_of_type(resolved[:node], :call)
    assert_equal 1, call[:resolved_group_num]
  end

  def test_subexp_call_with_zero_group_num_is_allowed_with_named_groups
    resolved = preprocess_pattern('(?<foo>a)\g<0>')
    call = nodes_of_type(resolved[:node], :call).last
    assert_equal 0, call[:resolved_group_num]
  end

  def test_subexp_call_rejects_ambiguous_named_groups
    assert_preprocess_error('(?<foo>a)(?<foo>b)\g<foo>', :ambiguous_named_subexp_call)
  end

  def test_subexp_call_rejects_unknown_name
    assert_preprocess_error('(?<foo>a)\g<bar>', :undefined_named_subexp_call)
  end

  def test_subexp_call_rejects_out_of_range_numeric_group
    assert_preprocess_error('(a)\g<2>', :subexp_call_group_num_out_of_range)
  end

  def test_subexp_call_with_zero_group_num_is_allowed_without_named_groups
    resolved = preprocess_pattern('a\g<0>')
    call = first_node_of_type(resolved[:node], :call)
    assert_equal 0, call[:resolved_group_num]
  end

  def test_sets_is_empty_on_group_call_and_back_ref
    resolved = preprocess_pattern('(a*)\g<1>\k<1>')
    assert_equal true, resolved[:node][:is_empty]
    nodes_of_type(resolved[:node], :group).each { |node| assert_equal true, node[:is_empty] }
    nodes_of_type(resolved[:node], :call).each { |node| assert_equal true, node[:is_empty] }
    nodes_of_type(resolved[:node], :back_ref).each { |node| assert_equal true, node[:is_empty] }
  end

  def test_raises_on_left_recursive_subexp_call
    assert_preprocess_error('(?<foo>\g<foo>)', :left_recursive_subexp_call)
  end

  def test_raises_on_left_recursive_subexp_call_for_g0
    assert_preprocess_error('\g<0>', :left_recursive_subexp_call)
  end

  def test_conditional_evaluates_branch_nodes_for_is_empty
    resolved = preprocess_pattern('(a)(?(1)a*|b)')
    conditional = first_node_of_type(resolved[:node], :conditional)
    assert_equal true, conditional[:yes_child][:is_empty]
    assert_equal false, conditional[:no_child][:is_empty]
    assert_equal true, conditional[:is_empty]
  end

  def test_raises_on_right_recursive_subexp_call_in_lookbehind
    assert_preprocess_error('(?<foo>(?<=x\g<foo>))', :right_recursive_subexp_call_in_lookbehind)
  end

  def test_raises_on_right_recursive_subexp_call_even_if_group_was_called_from_left
    assert_preprocess_error('(?<foo>x\g<foo>)\g<foo>(?<=\g<foo>)', :right_recursive_subexp_call_in_lookbehind)
  end

  def test_does_not_raise_left_recursion_for_group_only_reachable_through_zero_quantifier
    resolved = preprocess_pattern('(?<foo>\g<foo>x){0}(?<=\g<foo>)')
    group = first_node_of_type(resolved[:node], :group)
    assert_equal true, resolved[:node][:is_empty]
    assert_equal false, group[:is_empty]
  end

  private

  def preprocess_pattern(pattern, **)
    NarakuRuby.preprocess(NarakuRuby.parse(pattern, **))
  end

  def assert_preprocess_error(pattern, code)
    error = assert_raises(NarakuRuby::PreprocessError) { preprocess_pattern(pattern) }
    assert_equal code, error.code
  end

  def nodes_of_type(root, type)
    find_nodes(root) { |node| node[:type] == type }
  end

  def first_node_of_type(root, type)
    nodes_of_type(root, type).first
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
