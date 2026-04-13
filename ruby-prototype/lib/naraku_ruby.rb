require_relative 'naraku_ruby/mruby_bridge'
require_relative 'naraku_ruby/group_number_resolver'

module NarakuRuby
  def self.parse(pattern, **options)
    bridge = MRubyBridge.new(script_path: File.expand_path('./mruby-scripts/parse.rb', __dir__))
    request = {
      pattern: pattern,
      options: options,
    }
    bridge.execute(request)
  end

  def self.preprocess(parsed)
    return parsed unless parsed.is_a?(Hash)

    node = parsed[:node]
    return parsed unless node.is_a?(Hash)

    GroupNumberResolver.rewrite!(node)
    parsed
  end
end
