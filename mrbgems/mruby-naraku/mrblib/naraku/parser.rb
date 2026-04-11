module Naraku
  class Parser
    class << self
      private :_new
    end

    def self.new(enc, pattern, **options)
      is_extended_mode = options.include?(:is_extended_mode) ? options[:is_extended_mode] : false
      is_ignore_case = options.include?(:is_ignore_case) ? options[:is_ignore_case] : false
      dot_allows_newline = options.include?(:dot_allows_newline) ? options[:dot_allows_newline] : false
      char_class_is_strict = options.include?(:char_class_is_strict) ? options[:char_class_is_strict] : false
      char_type_is_ascii_only = options.include?(:char_type_is_ascii_only) ? options[:char_type_is_ascii_only] : true
      posix_char_class_is_ascii_only = options.include?(:posix_char_class_is_ascii_only) ? options[:posix_char_class_is_ascii_only] : false
      fold_flags = options.include?(:fold_flags) ? options[:fold_flags] : []

      fold_flags = Naraku::Encoding.parse_fold_flags(fold_flags) if fold_flags.is_a?(Array)

      parser = _new(
        enc,
        pattern,
        is_extended_mode,
        is_ignore_case,
        dot_allows_newline,
        char_class_is_strict,
        char_type_is_ascii_only,
        posix_char_class_is_ascii_only,
        fold_flags
      )
      parser.set_pattern(enc, pattern)
      parser
    end

    attr_reader :enc, :pattern

    def set_pattern(enc, pattern)
      @enc = enc
      @pattern = pattern
    end
  end
end