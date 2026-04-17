module Naraku
  def self.parse_fold_flags(options)
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
