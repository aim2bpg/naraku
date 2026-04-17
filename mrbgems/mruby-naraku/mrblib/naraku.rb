module Naraku
  def self.parse_fold_flags(options)
    return options[0] if options.length == 1 && options[0].is_a?(Integer)

    flags = FOLD_DEFAULT
    options.each do |option|
      case option
      when :full
        flags |= FOLD_FULL
      when :turkish_azeri
        flags |= FOLD_TURKISH_AZERI
      when :ascii_only
        flags |= FOLD_ASCII_ONLY
      else
        raise ArgumentError, "invalid option: #{option}"
      end
    end
    flags
  end
end
