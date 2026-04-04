# frozen_string_literal: true

require_relative './ucd'

module Unicode
  class CaseMap
    def initialize(version)      
      @version = version

      @fold_items = {}
      @case_map_items = {}

      @core_props = nil # for `Cased` and `Case_Ignorable`
      @props = nil # for `Soft_Dotted`
      @ccc = nil

      case_map = UCD.load_case_map(version)
      case_map.each do |from_code, map|
        to_codes = nil
        to_types = []
        specials = []
        %i[fold fold_full lower upper title].each do |type|
          next if map[type] == [from_code]

          if to_codes.nil?
            to_types << type
            to_codes = map[type]
          else
            if to_codes == map[type]
              to_types << type
            else
              specials << { to_type: type, to_codes: map[type] }
              if type == :upper && map[type] == map[:title]
                specials.last[:to_type] = :upper_title
                break
              end
            end
          end
        end

        is_title = [from_code] == map[:title] && [from_code] != map[:lower] && [from_code] != map[:upper]
        if is_title
          swap_codes =
            case from_code
            when 0x01C5 then [0x0064, 0x017D] # LATIN CAPITAL LETTER D WITH SMALL LETTER Z WITH CARON (ǅ)
            when 0x01C8 then [0x006C, 0x004A] # LATIN CAPITAL LETTER L WITH SMALL LETTER J (ǈ)
            when 0x01CB then [0x006E, 0x004A] # LATIN CAPITAL LETTER N WITH SMALL LETTER J (ǋ)
            when 0x01F2 then [0x0064, 0x005A] # LATIN CAPITAL LETTER D WITH SMALL LETTER Z (ǲ)
            else
              # Other cases are Greek ligatures (U+1F88..U+1F8F, U+1F98..U+1F9F, U+1FA8..U+1FAF, U+1FBC, U+1FCC, and U+1FFC).
              [map[:fold_full].first, 0x0399]
            end
          specials << { to_type: :swap, to_codes: swap_codes }
        end

        item = { to_types:, to_codes:, specials: specials.reverse }

        if to_types.include?(:fold) || to_types.include?(:fold_full)
          @fold_items[from_code] = item
        else
          @case_map_items[from_code] = item
        end
      end

      @unfold_items = {}
      @fold_items.each do |from_code, item|
        to_codes = item[:to_codes]
        to_types = item[:to_types]

        @unfold_items[to_codes] ||= {
          fold: [],
          fold_and_fold_full: [],
          fold_full: [],
        }
        unfold_item = @unfold_items[to_codes]

        if to_types.include?(:fold)
          if to_types.include?(:fold_full)
            unfold_item[:fold_and_fold_full] << from_code
          end
        else
          unfold_item[:fold] << from_code
        end

        item[:specials].each do |special|
          if special[:to_type] == :fold_full
            to_codes = special[:to_codes]
            @unfold_items[to_codes] ||= {
              fold: [],
              fold_and_fold_full: [],
              fold_full: [],
            }
            @unfold_items[to_codes][:fold_full] << from_code
          end
        end
      end
    end

    attr_reader :fold_items, :case_map_items, :unfold_items

    def fold(codes, mode: :full, language: nil)
      is_string = false
      if codes.is_a?(String)
        is_string = true
        codes = codes.codepoints
      end

      if mode != :full && mode != :simple
        raise ArgumentError, "Invalid mode: #{mode}"
      end
      if language && language != :turkic
        raise ArgumentError, "Invalid language: #{language}"
      end

      type = mode == :full ? :fold_full : :fold
      is_turkic = language == :turkic

      result = codes.flat_map do |code|
        if is_turkic
          case code
          when 0x0049 # LATIN CAPITAL LETTER I
            next [0x0131] # LATIN SMALL LETTER DOTLESS I
          when 0x0130 # LATIN CAPITAL LETTER I WITH DOT ABOVE
            next [0x0069] # LATIN SMALL LETTER I
          end
        end

        item = @fold_items[code]
        next [code] unless item

        next item[:to_codes] if item[:to_types].include?(type)

        special = item[:specials].find { _1[:to_type] == type }
        special ? special[:to_codes] : [code]
      end

      result = result.pack('U*') if is_string
      result
    end

    def case_map(map_type, codes, context: false, language: nil)
      is_string = false
      if codes.is_a?(String)
        is_string = true
        codes = codes.codepoints
      end

      if !%i[lower upper title swap].include?(map_type)
        raise ArgumentError, "Invalid map_type: #{map_type}"
      end
      if language && !%i[turkic lithuanian].include?(language)
        raise ArgumentError, "Invalid language: #{language}"
      end

      is_turkic = language == :turkic
      is_lithuanian = language == :lithuanian
      context = true if is_turkic || is_lithuanian

      types =
        case map_type
        when :lower then [:lower]
        when :upper then [:upper, :upper_title]
        when :title then [:title, :upper_title]
        when :swap  then [:swap, :lower, :upper, :upper_title]
        end

      result = codes.each_with_index.flat_map do |code, index|
        types = [:lower] if index > 0 && types.include?(:title)

        # From:
        #   03A3; 03C2; 03A3; 03A3; Final_Sigma; # GREEK CAPITAL LETTER SIGMA
        if context && code == 0x03A3 && types.include?(:lower) && final_sigma?(codes, index)
          next [0x03C2]
        end

        if is_lithuanian
          # From:
          #   0307; 0307; ; ; lt After_Soft_Dotted; # COMBINING DOT ABOVE
          if code == 0x0307 && types.include?(:upper_title) && after_soft_dotted?(codes, index)
            next []
          end

          # From:
          #   0049; 0069 0307; 0049; 0049; lt More_Above; # LATIN CAPITAL LETTER I
          #   004A; 006A 0307; 004A; 004A; lt More_Above; # LATIN CAPITAL LETTER J
          #   012E; 012F 0307; 012E; 012E; lt More_Above; # LATIN CAPITAL LETTER I WITH OGONEK
          if (code == 0x0049 || code == 0x004A || code == 0x012E) && types.include?(:lower) && more_above?(codes, index)
            lower = code == 0x012E ? 0x012F : code + 0x20
            next [lower, 0x0307]
          end

          # From:
          #   00CC; 0069 0307 0300; 00CC; 00CC; lt; # LATIN CAPITAL LETTER I WITH GRAVE
          #   00CD; 0069 0307 0301; 00CD; 00CD; lt; # LATIN CAPITAL LETTER I WITH ACUTE
          #   0128; 0069 0307 0303; 0128; 0128; lt; # LATIN CAPITAL LETTER I WITH TILDE
          if (code == 0x00CC || code == 0x00CD || code == 0x0128) && types.include?(:lower)
            above = code == 0x00CC ? 0x0300 : code == 0x00CD ? 0x0301 : 0x0303
            next [0x0069, 0x0307, above]
          end
        end

        if is_turkic
          # From:
          #   0130; 0069; 0130; 0130; tr; # LATIN CAPITAL LETTER I WITH DOT ABOVE
          #   0130; 0069; 0130; 0130; az; # LATIN CAPITAL LETTER I WITH DOT ABOVE
          if code == 0x0130 && types.include?(:lower)
            next [0x0069]
          end

          # From:
          #   0307; ; 0307; 0307; tr After_I; # COMBINING DOT ABOVE
          #   0307; ; 0307; 0307; az After_I; # COMBINING DOT ABOVE
          if code == 0x0307 && types.include?(:lower) && after_i?(codes, index)
            next []
          end

          # From:
          #   0049; 0131; 0049; 0049; tr Not_Before_Dot; # LATIN CAPITAL LETTER I
          #   0049; 0131; 0049; 0049; az Not_Before_Dot; # LATIN CAPITAL LETTER I
          if code == 0x0049 && types.include?(:lower) && !before_dot?(codes, index)
            next [0x0131]
          end

          # From:
          #   0069; 0069; 0130; 0130; tr; # LATIN SMALL LETTER I
          #   0069; 0069; 0130; 0130; az; # LATIN SMALL LETTER I
          if code == 0x0069 && types.include?(:upper_title)
            next [0x0130]
          end
        end

        item = @fold_items[code] || @case_map_items[code]
        next [code] unless item

        special = item[:specials].find { types.include?(_1[:to_type]) }
        next special[:to_codes] if special

        next item[:to_codes] if types.any? { item[:to_types].include?(_1) }

        [code]
      end

      result = result.pack('U*') if is_string
      result
    end

    private

    def final_sigma?(codes, index)
      @core_props ||= UCD.load_core_props(@version)[0]
      cased = @core_props['Cased']
      case_ignorable = @core_props['Case_Ignorable']

      # Before C: /\p{cased} (\p{Case_Ignorable})*/
      found = false
      (0...index).reverse_each do |i|
        if cased.include?(codes[i])
          found = true
          break
        end

        break unless case_ignorable.include?(codes[i])
      end
      return false unless found

      # After C: not /\p{Case_Ignorable})* \p{cased}/
      (index + 1...codes.size).each do |i|
        return false if cased.include?(codes[i])

        break unless case_ignorable.include?(codes[i])
      end

      true
    end

    def after_soft_dotted?(codes, index)
      @props ||= UCD.load_props(@version)
      soft_dotted = @props['Soft_Dotted']

      @ccc ||= UCD.load_ccc(@version)
      ccc0 = @ccc['0']
      ccc230 = @ccc['230']

      # Before C: /[\p{Soft_Dotted}] ([^\p{ccc=230} \p{ccc=0}])*/
      (0...index).reverse_each do |i|
        return true if soft_dotted.include?(codes[i])
        break if ccc230.include?(codes[i]) || ccc0.include?(codes[i])
      end

      false
    end

    def more_above?(codes, index)
      @ccc ||= UCD.load_ccc(@version)
      ccc0 = @ccc['0']
      ccc230 = @ccc['230']

      # After C: /[^\p{ccc=230} \p{ccc=0}]* [\p{ccc=230}]/
      (index + 1...codes.size).each do |i|
        return true if ccc230.include?(codes[i])
        break unless ccc0.include?(codes[i])
      end

      false
    end

    def after_i?(codes, index)
      @ccc ||= UCD.load_ccc(@version)
      ccc0 = @ccc['0']
      ccc230 = @ccc['230']

      # Before C: /[I] ([^\p{ccc=230} \p{ccc=0}])*/
      (0...index).reverse_each do |i|
        return true if codes[i] == 0x0049
        break if ccc230.include?(codes[i]) || ccc0.include?(codes[i])
      end

      false
    end

    def before_dot?(codes, index)
      @ccc ||= UCD.load_ccc(@version)
      ccc0 = @ccc['0']
      ccc230 = @ccc['230']

      # After C: /([^\p{ccc=230} \p{ccc=0}])* [\u0307]/
      (index + 1...codes.size).each do |i|
        return true if codes[i] == 0x0307
        break unless ccc0.include?(codes[i]) || ccc230.include?(codes[i])
      end

      false
    end
  end
end
