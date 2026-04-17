module Naraku
  class Encoding
    def unicode?
      flags.anybits?(FLAG_UNICODE)
    end

    def self_sync?
      flags.anybits?(FLAG_SELF_SYNC)
    end

    private :_get_case_fold

    def case_fold(code, *options)
      flags = Naraku.parse_fold_flags(options)
      _get_case_fold(code, flags)
    end

    private :_expand_case_unfold

    def expand_case_unfold(codes, *options)
      flags = Naraku.parse_fold_flags(options)
      _expand_case_unfold(codes, flags)
    end

    private :_iterate_case_fold

    def iterate_case_fold(*options, &)
      flags = Naraku.parse_fold_flags(options)
      _iterate_case_fold(flags, &)
    end

    private :_cprop?

    def cprop?(code, cprop)
      cprop = Encoding.name_to_cprop(cprop) if cprop.is_a?(String)
      _cprop?(code, cprop)
    end

    private :_get_cprop_code_range

    def cprop_code_range(cprop)
      cprop = Encoding.name_to_cprop(cprop) if cprop.is_a?(String)

      result = _get_cprop_code_range(cprop)
      case result
      when :delegate_7bit
        delegate_range = (0x00..0x7F)
      when :delegate_8bit
        delegate_range = (0x00..0xFF)
      else
        return result
      end

      result = []
      range_begin = nil
      range_end = nil
      delegate_range.each do |code|
        next unless cprop?(code, cprop)

        unless !range_begin.nil? && code == range_end + 1
          result << (range_begin..range_end) if range_begin
          range_begin = code
        end
        range_end = code
      end
      result << (range_begin..range_end) if range_begin

      result
    end
  end
end
