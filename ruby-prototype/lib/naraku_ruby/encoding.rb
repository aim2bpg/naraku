# frozen_string_literal: true

module NarakuRuby
  module Encoding
    module_function

    def case_fold(bytes, *fold_flags)
      folded_bytes = []

      bytes.pack('C*').force_encoding(::Encoding::UTF_8).each_codepoint do |code|
        folded_bytes.concat(NarakuRuby.case_fold(code, *fold_flags).pack('U*').bytes)
      end

      folded_bytes
    end

    def expand_case_unfold(bytes, *fold_flags)
      nodes = [{ type: :match }]

      lasts = [
        0,
      ]
      last_three_codes = []

      bytes.pack('C*').force_encoding(::Encoding::UTF_8).each_codepoint.reverse_each do |code|
        last_three_codes.unshift(code)
        last_three_codes.pop if last_three_codes.size > 3

        nodes << {
          type: :code,
          code:,
          next: lasts[0],
        }
        currents = [nodes.length - 1]

        NarakuRuby.expand_case_unfold(last_three_codes, *fold_flags)&.each do |unfold|
          folded_codes_len = unfold[:folded_codes_len]
          unfolded_code = unfold[:unfolded_code]
          nodes << {
            type: :code,
            code: unfolded_code,
            next: lasts[folded_codes_len - 1],
          }
          currents << (nodes.length - 1)
        end

        current =
          if currents.size == 1
            currents[0]
          else
            nodes << { type: :alt, children: currents }
            nodes.length - 1
          end
        lasts.unshift(current)
        lasts.pop if lasts.size > 3
      end

      [lasts[0], nodes]
    end
  end
end
