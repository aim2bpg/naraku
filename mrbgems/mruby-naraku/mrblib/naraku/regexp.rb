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

    def initialize(string, regexp, caps, names_to_capture_nums = nil)
      @string = string
      @regexp = regexp
      @caps = caps
      @names_to_capture_nums = names_to_capture_nums || {}
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
    # Integer: positional index; String/Symbol: named capture.
    def [](n)
      case n
      when Integer
        b = byte_begin(n)
        e = byte_end(n)
        return nil if b.nil? || e.nil?

        @string.byteslice(b, e - b)
      when Symbol
        self[n.to_s]
      when String
        cap_nums = @names_to_capture_nums[n]
        return nil if cap_nums.nil?

        result = nil
        cap_nums.each do |num|
          v = self[num]
          result = v unless v.nil?
        end
        result
      end
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

    # Returns an array of capture group names (strings).
    def names
      @names_to_capture_nums.keys
    end

    # Returns a Hash mapping each capture name to its matched string (or nil).
    def named_captures
      result = {}
      @names_to_capture_nums.each_key { |name| result[name] = self[name] }
      result
    end

    def values_at(*indices)
      indices.map { |i| self[i] }
    end

    def inspect
      name_for = {}
      @names_to_capture_nums.each do |name, nums|
        nums.each { |n| name_for[n] ||= name }
      end
      s = "#<MatchData #{self[0].inspect}"
      i = 1
      while i < size
        label = name_for[i] || i.to_s
        s += " #{label}:#{self[i].inspect}"
        i += 1
      end
      "#{s}>"
    end
  end

  class Regexp
    def initialize(pattern, enc = Naraku::Encoding::UTF_8, **parser_options)
      @pattern = pattern
      parser = Naraku::Parser.new(enc, pattern, **parser_options)
      node = parser.parse
      parser.postprocess(node)
      @program = Naraku::Program._compile(parser, node)

      if parser.has_named_captures
        names_map = parser.capture_names_map
        entries   = parser.capture_entries
        @names_to_capture_nums = {}
        names_map.each do |name, entry_index|
          @names_to_capture_nums[name] = entries[entry_index][:capture_nums]
        end
      else
        @names_to_capture_nums = {}
      end
    end

    attr_reader :pattern
    alias source pattern

    def inspect
      "/#{@pattern.gsub('/', '\\/')}/"
    end

    def names
      @names_to_capture_nums.keys
    end

    # Returns {name => [capture_num, ...]} for all named groups.
    def named_captures
      result = {}
      @names_to_capture_nums.each { |name, nums| result[name] = nums }
      result
    end

    def ===(string)
      !match(string).nil?
    end

    # Searches `string` starting at byte offset `byte_start`.
    # Returns a MatchData on match, nil otherwise.
    def match(string, byte_start = 0)
      caps = @program._search(string, byte_start)
      return nil if caps.nil?

      MatchData.new(string, self, caps, @names_to_capture_nums)
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
