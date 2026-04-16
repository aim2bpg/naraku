require_relative 'naraku_ruby/mruby_bridge'

module NarakuRuby
  ENUM_VALUE_KEYS = %i[type target_kind assertion_type quantifier_type char_type posix_char_class].freeze

  def self.parse(pattern, postprocess: false, **options)
    bridge = MRubyBridge.new(script_path: File.expand_path('./mruby-scripts/parse.rb', __dir__))
    request = {
      pattern:,
      postprocess:,
      options:,
    }
    context = bridge.execute(request)
    normalize_enum_values!(context)
    context
  end

  def self.normalize_enum_values!(obj)
    case obj
    when Hash
      obj.each do |key, value|
        obj[key] = value.to_sym if ENUM_VALUE_KEYS.include?(key) && value.is_a?(String)
        normalize_enum_values!(obj[key])
      end
    when Array
      obj.each { |element| normalize_enum_values!(element) }
    end
  end
  private_class_method :normalize_enum_values!
end
