# frozen_string_literal: true

module Unicode
  # `RangeSet` represents a set of Unicode code points as a sorted array of
  # non-overlapping ranges.
  #
  # In this implementation, we assume each value in `@ranges` is non-exclusive
  # (i.e. the end of the range is included in the set).
  class RangeSet
    def initialize(*ranges)
      @ranges = []

      ranges.each { |range| self << range }
    end

    # Returns the total number of code points in the set.
    def size
      @ranges.reduce(0) { |sum, r| sum + (r.end - r.begin + 1) }
    end

    def range_size = @ranges.size

    # Checks if the set is empty.
    def empty? = size.zero?

    # Checks if the set only contains ASCII code points (U+0000 to U+007F).
    def ascii_only?
      @ranges.empty? || (@ranges.last.end <= 0x7F)
    end

    # Checks if the set only contains code points that can be represented in 8 bits (U+0000 to U+00FF).
    def ascii_8bit_only?
      @ranges.empty? || (@ranges.last.end <= 0xFF)
    end

    # Adds a code point or range to the set, merging it with existing ranges
    # if necessary.
    #
    # ```ruby
    # rs = RangeSet.new
    # rs << 100
    # rs << (100..200)
    # rs << (201..300)
    # rs.each { |r| p r } # => 100..300
    # ```
    def <<(range)
      range = range..range unless range.is_a?(Range)
      range = range.begin..(range.end - 1) if range.exclude_end?
      return self if range.begin > range.end

      new_start = range.begin
      new_end = range.end

      insert_idx = @ranges.bsearch_index { |r| r.begin >= new_start } || @ranges.size

      start_idx = insert_idx
      if insert_idx.positive? && @ranges[insert_idx - 1].end + 1 >= new_start
        start_idx = insert_idx - 1
        new_start = [@ranges[insert_idx - 1].begin, new_start].min
      end

      end_idx = start_idx
      while end_idx < @ranges.length && @ranges[end_idx].begin <= new_end + 1
        new_end = [@ranges[end_idx].end, new_end].max
        end_idx += 1
      end

      @ranges[start_idx...end_idx] = [new_start..new_end]

      self
    end

    # Removes a code point or range from the set, splitting existing ranges if
    # necessary.
    #
    # ```ruby
    # rs = RangeSet.new(100..200)
    # rs.delete(150)
    # rs.each { |r| p r } # => 100..149, 151..200
    # rs.delete(120..130)
    # rs.each { |r| p r } # => 100..119, 131..149, 151..200
    # ```
    def delete(range)
      range = range..range unless range.is_a?(Range)
      range = range.begin..(range.end - 1) if range.exclude_end?
      return self if range.begin > range.end

      del_start = range.begin
      del_end = range.end

      start_idx = @ranges.bsearch_index { |r| r.end >= del_start }
      return self unless start_idx

      i = start_idx
      i += 1 while i < @ranges.size && @ranges[i].begin <= del_end

      (i - 1).downto(start_idx) do |idx|
        existing = @ranges[idx]

        if existing.begin < del_start && existing.end > del_end
          @ranges[idx] = (existing.begin..(del_start - 1))
          @ranges.insert(idx + 1, (del_end + 1)..existing.end)
        elsif existing.begin < del_start && existing.end >= del_start
          @ranges[idx] = (existing.begin..(del_start - 1))
        elsif existing.begin <= del_end && existing.end > del_end
          @ranges[idx] = ((del_end + 1)..existing.end)
        else
          @ranges.delete_at(idx)
        end
      end

      self
    end

    # Checks if this set includes a given code point.
    #
    # ```ruby
    # rs = RangeSet.new(100..200)
    # rs.include?(150) # => true
    # rs.include?(250) # => false
    # rs.include?(120..130) # => true
    # rs.include?(90..110) # => false
    # rs.include?(190..210) # => false
    # ```
    def include?(range)
      range = range..range unless range.is_a?(Range)
      range = range.begin..(range.end - 1) if range.exclude_end?
      raise ArgumentError, "Invalid range: #{range}" if range.begin > range.end

      cp = range.begin
      idx = @ranges.bsearch_index { |r| cp <= r.end }
      return false unless idx

      @ranges[idx].include?(cp) && @ranges[idx].include?(range.end)
    end

    # Checks if this set is a subset of another set.
    #
    # ```ruby
    # rs1 = RangeSet.new(100..200)
    # rs2 = RangeSet.new(50..250)
    # rs1.subset_of?(rs2) # => true
    # rs2.subset_of?(rs1) # => false
    # ```
    def subset_of?(other)
      each_range.all? { |r| other.include?(r) }
    end

    # Returns a new `RangeSet` that is the union of this set and another set.
    #
    # ```ruby
    # rs1 = RangeSet.new(100..200)
    # rs2 = RangeSet.new(150..250)
    # rs3 = rs1 | rs2
    # rs3.each { |r| p r } # => 100..250
    # ````
    def |(other)
      result = self.class.new(*each_range)
      other.each_range { |r| result << r }
      result
    end

    # Returns a new `RangeSet` that is negation of this set (i.e. all code points not in the set).
    #
    # ```ruby
    # rs1 = RangeSet.new(100..200)
    # rs2 = ~rs1
    # rs2.each { |r| p r } # => 0..99, 201..0x10FFFF
    # ```
    def ~@
      result = self.class.new
      prev_end = -1

      each_range do |range|
        result << ((prev_end + 1)..(range.begin - 1)) if range.begin > prev_end + 1
        prev_end = range.end
      end

      result << ((prev_end + 1)..0x10FFFF) if prev_end < 0x10FFFF

      result
    end

    # Returns a new `RangeSet` that is the intersection of this set and another set.
    #
    # ```ruby
    # rs1 = RangeSet.new(100..200)
    # rs2 = RangeSet.new(150..250)
    # rs3 = rs1 & rs2
    # rs3.each { |r| p r } # => 150..200
    # ```
    def &(other)
      ~(~self | ~other)
    end

    # Returns a new `RangeSet` that is the difference of this set and another set.
    #
    # ```ruby
    # rs1 = RangeSet.new(100..200)
    # rs2 = RangeSet.new(150..250)
    # rs3 = rs1 - rs2
    # rs3.each { |r| p r } # => 100..149
    # ```
    def -(other)
      self & ~other
    end

    # Iterates over the ranges in this set.
    #
    # ```ruby
    # rs = RangeSet.new(100..200, 300..400)
    # rs.each { |r| p r } # => 100..200, 300..400
    # ```
    def each_range(&)
      return enum_for(:each_range) unless block_given?

      @ranges.each(&)
      self
    end

    # Clones this `RangeSet`.
    #
    # ```ruby
    # rs1 = RangeSet.new(100..200)
    # rs2 = rs1.dup
    # rs2 << (300..400)
    # rs1.each { |r| p r } # => 100..200
    # rs2.each { |r| p r } # => 100..200, 300..400
    # ```
    def dup
      self.class.new(*each_range)
    end
  end
end
