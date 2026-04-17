require_relative 'naraku_ruby/mruby_bridge'

module NarakuRuby
  ENUM_VALUE_KEYS = %i[type target_kind assertion_type quantifier_type char_type posix_char_class].freeze

  def self.parse(pattern, postprocess: false, **options)
    request = {
      pattern:,
      postprocess:,
      options:,
    }
    context = parse_bridge.execute(request)
    normalize_enum_values!(context)
    context
  end

  def self.case_fold(code, *fold_flags)
    encoding_bridge.execute({
      method: 'case_fold',
      code:,
      fold_flags:,
    })
  end

  def self.expand_case_unfold(codes, *fold_flags)
    encoding_bridge.execute({
      method: 'expand_case_unfold',
      codes:,
      fold_flags:,
    })
  end

  def self.iterate_case_fold(*fold_flags)
    encoding_bridge.execute({
      method: 'iterate_case_fold',
      fold_flags:,
    })
  end

  def self.cprop_code_range(cprop)
    ranges = encoding_bridge.execute({
      method: 'cprop_code_range',
      cprop:,
    })
    ranges.map { |range| range[:begin]..range[:end] }
  end

  def self.clear_cache!
    encoding_bridge.clear_cache!
  end

  def self.parse_bridge
    @parse_bridge ||= MRubyBridge.new(
      script_path: File.expand_path('./mruby-scripts/parse.rb', __dir__),
      use_cache: true
    )
  end
  private_class_method :parse_bridge

  def self.encoding_bridge
    @encoding_bridge ||= MRubyBridge.new(
      script_path: File.expand_path('./mruby-scripts/encoding.rb', __dir__),
      use_cache: true
    )
  end
  private_class_method :encoding_bridge

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

require_relative 'naraku_ruby/encoding'
require_relative 'naraku_ruby/char_class'
require_relative 'naraku_ruby/char_class_builder'
require_relative 'naraku_ruby/dfa'
