require_relative 'test_helper'

class TestGroupNumberResolver < Minitest::Test
  def test_rewrite_named_and_unnamed_groups
    parsed = {
      node: {
        type: :concat,
        children: [
          { type: :group, has_name: false, group_num: 1, child: { type: :literal } },
          { type: :group, has_name: true, name: 'foo', group_num: 0, child: { type: :literal } },
          { type: :group, has_name: false, group_num: 2, child: { type: :literal } },
          { type: :group, has_name: true, name: 'bar', group_num: 0, child: { type: :literal } },
        ],
      },
    }

    NarakuRuby.preprocess(parsed)

    group_nums = parsed[:node][:children].map { |n| n[:group_num] }
    assert_equal [0, 1, 0, 2], group_nums
  end

  def test_no_named_group_keeps_original_group_nums
    parsed = {
      node: {
        type: :concat,
        children: [
          { type: :group, has_name: false, group_num: 1, child: { type: :literal } },
          { type: :group, has_name: false, group_num: 2, child: { type: :literal } },
        ],
      },
    }

    NarakuRuby.preprocess(parsed)

    group_nums = parsed[:node][:children].map { |n| n[:group_num] }
    assert_equal [1, 2], group_nums
  end
end
