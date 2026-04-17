# frozen_string_literal: true

require 'json'
require 'open3'

module NarakuRuby
  class MRubyBridgeError < StandardError; end

  class MRubyBridgeResponseError < MRubyBridgeError
    def initialize(response)
      super(response[:message_full] || response[:message])
      @response = response
    end

    attr_reader :response
  end

  class MRubyBridge
    MRUBY_BIN = File.expand_path('../../../bin/mruby', __dir__)

    def initialize(
      script_path:, mruby_bin: MRUBY_BIN, use_cache: false
    )
      @mruby_bin = mruby_bin
      @script_path = script_path
      @use_cache = use_cache
      @cache = {}
    end

    def execute(request)
      cache_key = nil
      if @use_cache
        cache_key = JSON.generate(request)
        cached_response = @cache[cache_key]
        return deep_copy(cached_response) unless cached_response.nil?
      end

      stdout, stderr, status =
        Open3.capture3(@mruby_bin, @script_path, stdin_data: JSON.generate(request))

      raise MRubyBridgeError, "mruby failed: #{stderr.strip}" unless status.success?

      response = JSON.parse(stdout, symbolize_names: true)
      raise MRubyBridgeResponseError, response[:error] unless response[:ok]

      @cache[cache_key] = response[:data] if @use_cache
      deep_copy(response[:data])
    end

    def clear_cache!
      @cache.clear
    end

    private

    def deep_copy(obj)
      Marshal.load(Marshal.dump(obj))
    end
  end
end
