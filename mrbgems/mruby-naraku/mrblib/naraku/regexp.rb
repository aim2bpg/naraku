module Naraku
  class CompileError < Error
    attr_reader :error_code, :offset, :length

    def initialize(error_code, offset, length)
      @error_code = error_code
      @offset = offset
      @length = length
      super(build_message)
    end

    private

    def build_message
      err_msg = Naraku.error_message(@error_code)
      len = @length || 0
      span_end = @offset + len
      if len.positive?
        "#{err_msg} (at span #{@offset}...#{span_end})"
      else
        "#{err_msg} (at offset #{@offset})"
      end
    end
  end

  class MatchData
    attr_reader :string, :regexp

    def initialize(string, regexp, caps)
      @string = string
      @regexp = regexp
      @caps = caps
    end

    # Returns the byte offset of the beginning of the nth capture (0 = whole match).
    def byte_begin(n)
      @caps[n * 2]
    end

    # Returns the byte offset of the end of the nth capture (0 = whole match).
    def byte_end(n)
      @caps[(n * 2) + 1]
    end

    # Returns the matched string of the nth capture (0 = whole match).
    def [](n)
      b = byte_begin(n)
      e = byte_end(n)
      return nil if b.nil? || e.nil?

      @string.byteslice(b, e - b)
    end

    def size
      @caps.size / 2
    end
    alias length size

    def captures
      result = []
      i = 1
      while i < size
        result << self[i]
        i += 1
      end
      result
    end

    def to_a
      result = []
      i = 0
      while i < size
        result << self[i]
        i += 1
      end
      result
    end

    def pre_match
      b = byte_begin(0)
      return nil if b.nil?

      @string.byteslice(0, b)
    end

    def post_match
      e = byte_end(0)
      return nil if e.nil?

      @string.byteslice(e, @string.bytesize - e)
    end

    def to_s
      self[0]
    end
  end

  class Regexp
    def initialize(pattern, enc = Naraku::Encoding::UTF_8, **parser_options)
      @pattern = pattern
      parser = Naraku::Parser.new(enc, pattern, **parser_options)
      node = parser.parse
      parser.postprocess(node)
      @program = Naraku::Program._compile(parser, node)
    end

    attr_reader :pattern

    # Searches `string` starting at byte offset `byte_start`.
    # Returns a MatchData on match, nil otherwise.
    def match(string, byte_start = 0)
      caps = @program._search(string, byte_start)
      return nil if caps.nil?

      MatchData.new(string, self, caps)
    end

    def match?(string, byte_start = 0)
      @program._search_boolean(string, byte_start)
    end

    def =~(string)
      md = match(string)
      return nil if md.nil?

      md.byte_begin(0)
    end
  end
end
