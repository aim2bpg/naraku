require_relative 'naraku_ruby/mruby_bridge'
require_relative 'naraku_ruby/errors'
require_relative 'naraku_ruby/preprocessor'
require_relative 'naraku_ruby/preprocessors/group_number_resolver'
require_relative 'naraku_ruby/preprocessors/group_ref_resolver'
require_relative 'naraku_ruby/preprocessors/empty_match_analyzer'

module NarakuRuby
  ENUM_VALUE_KEYS = %i[type assertion_type quantifier_type char_type posix_char_class].freeze

  PREPROCESSORS = [
    Preprocessors::GroupNumberResolver.new,
    Preprocessors::GroupRefResolver.new,
    Preprocessors::EmptyMatchAnalyzer.new,
  ].freeze

  def self.parse(pattern, **options)
    bridge = MRubyBridge.new(script_path: File.expand_path('./mruby-scripts/parse.rb', __dir__))
    request = {
      pattern:,
      options:,
    }
    context = bridge.execute(request)
    normalize_enum_values!(context)
    context
  end

  def self.preprocess(context)
    return context unless context.is_a?(Hash)
    return context unless context[:node].is_a?(Hash)

    PREPROCESSORS.reduce(context) { |ctx, preprocessor| preprocessor.call(ctx) }
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
