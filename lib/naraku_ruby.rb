require_relative './naraku_ruby/mruby_bridge'

module NarakuRuby
  def self.parse(pattern, **options)
    bridge = MRubyBridge.new(script_path: File.expand_path('../mruby-scripts/parse.rb', __FILE__))
    request = {
      pattern: pattern,
      options: options,
    }
    bridge.execute(request)
  end
end
