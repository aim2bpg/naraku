# frozen_string_literal: true

require_relative '../test_helper'

module NarakuRuby
  class MRubyBridgeTest < Minitest::Test
    def test_response_error_exposes_message_and_response
      response = { message: 'something went wrong', ok: false }
      err = MRubyBridgeResponseError.new(response)
      assert_equal 'something went wrong', err.message
      assert_equal response, err.response
    end

    def test_response_error_prefers_message_full_over_message
      response = { message: 'short', message_full: 'long detailed message', ok: false }
      err = MRubyBridgeResponseError.new(response)
      assert_equal 'long detailed message', err.message
    end

    def test_response_error_is_a_mruby_bridge_error
      err = MRubyBridgeResponseError.new({ message: 'test' })
      assert_kind_of MRubyBridgeError, err
    end
  end
end
