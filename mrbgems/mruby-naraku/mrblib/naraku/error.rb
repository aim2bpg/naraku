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

      msg = "#{err_msg} (at offset #{@offset})"
      shows_details =
        @offset && @offset >= 0 && enc.min_mbc_width == 1 && pattern && pattern.chars.all? { |c| (0x20...0x7F).include?(c.ord) }

      if shows_details
        msg << "\n  /#{pattern}/\n"
        len = @length || 0
        marker_len = (len > 0 ? len : 1) - 1
        msg << '   ' + (' ' * @offset) + '^' + ('~' * marker_len)
      end

      msg
    end
  end
end
