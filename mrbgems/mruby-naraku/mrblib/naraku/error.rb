module Naraku
  class Error < StandardError; end

  class ParseError < Error
    attr_reader :parser, :error_code, :offset, :length

    def initialize(parser, error_code, offset, length)
      @parser = parser
      @error_code = error_code
      @offset = offset
      @length = length
      super(build_message(parser))
    end

    private

    def build_message(parser)
      err_msg = Naraku.error_message(@error_code)
      pattern = parser.pattern
      enc = parser.enc

      len = @length || 0
      span_end = @offset + len
      msg = if len.positive?
              "#{err_msg} (at span #{@offset}...#{span_end})"
            else
              "#{err_msg} (at offset #{@offset})"
            end
      shows_details =
        @offset && @offset >= 0 && enc.min_mbc_width == 1 && pattern&.each_byte&.all? { |b| (0x20...0x7F).include?(b) }

      if shows_details
        msg << "\n  /#{pattern}/\n"
        marker_len = (len.positive? ? len : 1) - 1
        msg << "   #{' ' * @offset}^#{'~' * marker_len}"
      end

      msg
    end
  end
end
