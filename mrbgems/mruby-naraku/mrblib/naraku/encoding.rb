module Naraku
  class Encoding
    NK_ENC_FLAG_UNICODE = 1 << 0
    NK_ENC_FLAG_SELF_SYNC = 1 << 1

    NK_FOLD_DEFAULT = 0
    NK_FOLD_FULL = 1 << 0
    NK_FOLD_TURKISH_AZERI = 1 << 1
    NK_FOLD_ASCII_ONLY = 1 << 2

    def unicode?
      (flags & NK_ENC_FLAG_UNICODE) != 0
    end

    def self_sync?
      (flags & NK_ENC_FLAG_SELF_SYNC) != 0
    end

    def parse_options(options)
      flags = NK_FOLD_DEFAULT
      options.each do |option|
        case option
        when :full
          flags |= NK_FOLD_FULL
        when :turkish_azeri
          flags |= NK_FOLD_TURKISH_AZERI
        when :ascii_only
          flags |= NK_FOLD_ASCII_ONLY
        else
          raise ArgumentError, "invalid option: #{option}"
        end
      end
      flags
    end

    def case_fold(code, *options)
      flags = parse_options(options)
      _get_case_fold(code, flags)
    end

    def expand_case_unfold(codes, *options)
      flags = parse_options(options)
      _expand_case_unfold(codes, flags)
    end

    def iterate_case_fold(*options, &block)
      flags = parse_options(options)
      _iterate_case_fold(flags, &block)
    end

    def ctype_code_range(ctype)
      result = _get_ctype_code_range(ctype)
      case result
      when :'delegate_7bit'
        delegate_range = (0x00..0x7F)
      when :'delegate_8bit'
        delegate_range = (0x00..0xFF)
      else
        return result
      end

      result = []
      range_begin = nil
      range_end = nil
      delegate_range.each do |code|
        if ctype?(code, ctype)
          if range_begin != nil && code == range_end + 1
            range_end = code
          else
            result << (range_begin..range_end) if range_begin
            range_begin = code
            range_end = code
          end
        end
      end
      result << (range_begin..range_end) if range_begin

      result
    end
  end
end
