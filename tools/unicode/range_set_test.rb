# frozen_string_literal: true

require 'test/unit'

require_relative './range_set'

module Unicode
  class RangeSetTest < Test::Unit::TestCase
    def test_insert
      rs = RangeSet.new

      rs << 100
      assert_equal [100..100], rs.each_range.to_a

      rs << (100..200)
      assert_equal [100..200], rs.each_range.to_a

      rs << (150..250)
      assert_equal [100..250], rs.each_range.to_a

       rs << (300..400)
       assert_equal [100..250, 300..400], rs.each_range.to_a

       rs << (255..295)
       assert_equal [100..250, 255..295, 300..400], rs.each_range.to_a

       rs << (251..299)
       assert_equal [100..400], rs.each_range.to_a

      rs << (50..500)
      assert_equal [50..500], rs.each_range.to_a
    end

    def test_delete
      rs = RangeSet.new(100..200, 300..400)

      rs.delete(150)
      assert_equal [100..149, 151..200, 300..400], rs.each_range.to_a

      rs.delete(120..130)
      assert_equal [100..119, 131..149, 151..200, 300..400], rs.each_range.to_a
    end

    def test_include
      rs = RangeSet.new(100..200, 300..400)

      assert rs.include?(150)
      assert rs.include?(350)
      assert !rs.include?(250)
      assert !rs.include?(450)

      assert rs.include?(120..130)
      assert !rs.include?(90..110)
      assert !rs.include?(190..210)
    end

    def test_subset_of
      rs1 = RangeSet.new(100..200)
      rs2 = RangeSet.new(50..250)
      rs3 = RangeSet.new(150..250)

      assert rs1.subset_of?(rs2)
      assert !rs2.subset_of?(rs1)
      assert !rs1.subset_of?(rs3)
      assert rs3.subset_of?(rs2)
    end

    def test_union
      rs1 = RangeSet.new(100..200)
      rs2 = RangeSet.new(150..250)
      rs3 = rs1 | rs2
      assert_equal [100..250], rs3.each_range.to_a
    end

    def test_negation
      rs1 = RangeSet.new(100..200)
      rs2 = ~rs1
      assert_equal [0..99, 201..0x10FFFF], rs2.each_range.to_a
    end

    def test_intersection
      rs1 = RangeSet.new(100..200)
      rs2 = RangeSet.new(150..250)
      rs3 = rs1 & rs2
      assert_equal [150..200], rs3.each_range.to_a
    end

    def test_difference
      rs1 = RangeSet.new(100..200)
      rs2 = RangeSet.new(150..250)
      rs3 = rs1 - rs2
      assert_equal [100..149], rs3.each_range.to_a
    end
  end
end
