# frozen_string_literal: true

require_relative 'dfa/compiler'
require_relative 'dfa/program'

module NarakuRuby
  module DFA
    def self.compile(context, **parser_options)
      full_dfa = parser_options.delete(:full_dfa) { false }
      full_dfa_eval = parser_options.delete(:eval) { false }
      context = NarakuRuby.parse(context, postprocess: true, **parser_options) if context.is_a?(String)
      Compiler.new(context, full_dfa:, full_dfa_eval:).compile
    end

    def self.match(context, string, pos = 0, **parser_options)
      compile(context, **parser_options).match(string, pos)
    end

    def self.match?(context, string, pos = 0, **parser_options)
      !!match(context, string, pos, **parser_options)
    end
  end
end
