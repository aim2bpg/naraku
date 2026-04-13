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
    MRUBY_BIN = File.expand_path('../../../bin/mruby', __FILE__)

    def initialize(
      mruby_bin: MRUBY_BIN,
      script_path:
    )
      @mruby_bin = mruby_bin
      @script_path = script_path
    end

    def execute(request)
      stdout, stderr, status =
        Open3.capture3(@mruby_bin, @script_path, stdin_data: JSON.generate(request))

      unless status.success?
        raise MRubyBridgeError, "mruby failed: #{stderr.strip}"
      end

      response = JSON.parse(stdout, symbolize_names: true)
      unless response[:ok]
        raise MRubyBridgeResponseError.new(response[:error])
      end

      response[:data]
    end
  end
end