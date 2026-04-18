# frozen_string_literal: true

module NarakuRuby
  class CharClass
    UNICODE_MAX = 0x10_FFFF
    ASCII_MAX = 0x7F

    def initialize(ranges = [])
      @ranges = []
      ranges.each { |range| add(range) }
    end

    def ranges
      @ranges.dup
    end

    def include?(code)
      validate_codepoint!(code)
      fast_include?(code)
    end

    def fast_include?(code)
      left = 0
      right = @ranges.length - 1
      while left <= right
        mid = (left + right) / 2
        range = @ranges[mid]
        if code < range.begin
          right = mid - 1
        elsif code > range.end
          left = mid + 1
        else
          return true
        end
      end
      false
    end

    def add(value)
      start_code, end_code = normalize_value(value)
      return self if start_code.nil?

      left_bound = start_code.zero? ? 0 : start_code - 1
      right_bound = end_code == UNICODE_MAX ? UNICODE_MAX : end_code + 1

      left = lower_bound_by_end(left_bound)
      right = upper_bound_by_begin(right_bound)

      if left < right
        start_code = [start_code, @ranges[left].begin].min
        end_code = [end_code, @ranges[right - 1].end].max
      end

      merged = @ranges[0...left]
      merged << (start_code..end_code)
      merged.concat(@ranges[right..]) if right < @ranges.length
      @ranges = merged
      self
    end

    def delete(value)
      start_code, end_code = normalize_value(value)
      return self if start_code.nil?

      left = lower_bound_by_end(start_code)
      right = upper_bound_by_begin(end_code)
      return self if left >= right

      trimmed = @ranges[0...left]
      @ranges[left...right].each do |range|
        trimmed << (range.begin..(start_code - 1)) if range.begin < start_code
        trimmed << ((end_code + 1)..range.end) if end_code < range.end
      end
      trimmed.concat(@ranges[right..]) if right < @ranges.length
      @ranges = trimmed
      self
    end

    def union(other)
      other = normalize_char_class!(other)
      result = CharClass.new(@ranges)
      other.ranges.each { |range| result.add(range) }
      result
    end

    def intersect(other)
      other = normalize_char_class!(other)
      result = CharClass.new

      right = other.ranges
      @ranges.each do |left_range|
        j = lower_bound_by_end(left_range.begin, right)
        while j < right.length && right[j].begin <= left_range.end
          overlap_begin = [left_range.begin, right[j].begin].max
          overlap_end = [left_range.end, right[j].end].min
          result.add(overlap_begin..overlap_end)
          j += 1
        end
      end
      result
    end

    def negate(min_code = 0, max_code = UNICODE_MAX)
      validate_codepoint!(min_code)
      validate_codepoint!(max_code)
      raise ArgumentError, 'min_code must be <= max_code' if min_code > max_code

      result = CharClass.new
      cursor = min_code

      i = lower_bound_by_end(min_code)
      while i < @ranges.length
        range = @ranges[i]
        break if range.begin > max_code

        clipped_begin = [range.begin, min_code].max
        clipped_end = [range.end, max_code].min

        result.add(cursor..(clipped_begin - 1)) if cursor < clipped_begin
        cursor = clipped_end + 1
        break if cursor > max_code

        i += 1
      end

      result.add(cursor..max_code) if cursor <= max_code
      result
    end

    def case_fold(*fold_flags)
      folded_char_class = CharClass.new(@ranges)
      expanded_strings = Set.new

      NarakuRuby.iterate_case_fold(*fold_flags).each do |entry|
        code = entry[:code]
        next unless include?(code)

        folded_codes = entry[:folded_codes]
        if folded_codes.length == 1
          folded_char_class.add(folded_codes[0])
        else
          expanded_strings.add(folded_codes.pack('U*'))
        end
      end

      unfolded_char_class = CharClass.new(folded_char_class.ranges)
      NarakuRuby.iterate_case_fold(*fold_flags).each do |entry|
        folded_codes = entry[:folded_codes]
        if folded_codes.length == 1
          unfolded_char_class.add(entry[:code]) if folded_char_class.include?(folded_codes[0])
        elsif expanded_strings.include?(folded_codes.pack('U*'))
          unfolded_char_class.add(entry[:code])
        end
      end

      [unfolded_char_class, expanded_strings]
    end

    private

    def normalize_char_class!(other)
      return other if other.is_a?(CharClass)

      raise ArgumentError, 'other must be NarakuRuby::CharClass'
    end

    def normalize_value(value)
      case value
      when Integer
        validate_codepoint!(value)
        [value, value]
      when Range
        range_begin = value.begin
        range_end = value.end
        raise ArgumentError, 'range must have Integer endpoints' unless range_begin.is_a?(Integer) && range_end.is_a?(Integer)

        range_end -= 1 if value.exclude_end?
        return [nil, nil] if range_begin > range_end

        validate_codepoint!(range_begin)
        validate_codepoint!(range_end)
        [range_begin, range_end]
      else
        raise ArgumentError, 'value must be Integer or Range'
      end
    end

    def validate_codepoint!(code)
      return if code.is_a?(Integer) && code >= 0 && code <= UNICODE_MAX

      raise ArgumentError, "invalid codepoint: #{code.inspect}"
    end

    def lower_bound_by_end(value, ranges = @ranges)
      left = 0
      right = ranges.length
      while left < right
        mid = (left + right) / 2
        if ranges[mid].end < value
          left = mid + 1
        else
          right = mid
        end
      end
      left
    end

    def upper_bound_by_begin(value, ranges = @ranges)
      left = 0
      right = ranges.length
      while left < right
        mid = (left + right) / 2
        if ranges[mid].begin <= value
          left = mid + 1
        else
          right = mid
        end
      end
      left
    end
  end
end
