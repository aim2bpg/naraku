# frozen_string_literal: true

module NarakuRuby
  class PreprocessError < StandardError
    def initialize(code, message)
      @code = code
      super(message)
    end

    attr_reader :code
  end
end
