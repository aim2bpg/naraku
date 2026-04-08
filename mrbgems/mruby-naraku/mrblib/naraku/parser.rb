module Naraku
  class Parser
    class << self
      private :_new
    end

    def self.new(enc, pattern, **options)
      is_extended_mode = options[:is_extended_mode] || false
      is_ignore_case = options[:is_ignore_case] || false
      dot_allows_newline = options[:dot_allows_newline] || false
      char_class_is_strict = options[:char_class_is_strict] || false
      char_prop_is_ascii_only = options[:char_prop_is_ascii_only] || false
      posix_char_class_is_ascii_only = options[:posix_char_class_is_ascii_only] || false
      fold_flags = options[:fold_flags] || []

      fold_flags = Naraku::Encoding.parse_fold_flags(fold_flags) if fold_flags.is_a?(Array)

      _new(
        enc,
        pattern,
        is_extended_mode,
        is_ignore_case,
        dot_allows_newline,
        char_class_is_strict,
        char_prop_is_ascii_only,
        posix_char_class_is_ascii_only,
        fold_flags
      )
    end
  end
end