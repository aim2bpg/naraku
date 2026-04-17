# frozen_string_literal: true

require_relative 'range_set'

module Unicode
  # `UCD` provides methods to load various data from the Unicode Character
  # Database (UCD).
  #
  # This module also provides method to load emoji sequence data.
  module UCD
    extend self

    # Loads `extracted/DerivedName.txt` from the UCD for the given version, and
    # returns a hash mapping character names to their code points.
    #
    # ```ruby
    # names = Unicode::UCD.load_name('17.0.0')
    #
    # names['LATIN CAPITAL LETTER A'] # => 0x0041
    # ```
    def load_name(version)
      names = {}
      load_file_cp(version, 'extracted/DerivedName.txt') do |range, name|
        range.each do |cp|
          unless name.include?('*')
            names[name] = cp
            next
          end

          cp_hex = if cp <= 0xFFFF
                     cp.to_s(16).upcase.rjust(4, '0')
                   else
                     cp.to_s(16).upcase.rjust(6, '0')
                   end

          names[name.sub('*', cp_hex)] = cp
        end
      end

      names
    end

    # Loads `extracted/DerivedGeneralCategory.txt` from the UCD for the given
    # version, and returns a hash mapping value names of the `General_Category`
    # property to their code point ranges.
    #
    # Note that each key in the result is a short name for the property value.
    #
    # ```ruby
    # gc = Unicode::UCD.load_gc('17.0.0')
    #
    # gc['L'].include?(0x0041) # => true
    # ```
    def load_gc(version)
      gc = {}
      load_file_cp(version, 'extracted/DerivedGeneralCategory.txt') do |range, category|
        gc[category] ||= RangeSet.new
        gc[category] << range
      end

      gc['LC'] = gc['Lu'] | gc['Ll'] | gc['Lt']
      gc['L'] = gc['Lu'] | gc['Ll'] | gc['Lt'] | gc['Lm'] | gc['Lo']
      gc['M'] = gc['Mn'] | gc['Mc'] | gc['Me']
      gc['N'] = gc['Nd'] | gc['Nl'] | gc['No']
      gc['P'] = gc['Pc'] | gc['Pd'] | gc['Ps'] | gc['Pe'] | gc['Pi'] | gc['Pf'] | gc['Po']
      gc['S'] = gc['Sm'] | gc['Sc'] | gc['Sk'] | gc['So']
      gc['Z'] = gc['Zs'] | gc['Zl'] | gc['Zp']
      gc['C'] = gc['Cc'] | gc['Cf'] | gc['Cs'] | gc['Co'] | gc['Cn']

      gc
    end

    # Loads `Scripts.txt` from the UCD for the given version, and returns a hash
    # mapping value names of the `Script` property to their code point ranges.
    #
    # Note that each key in the result is a long name for the property value.
    #
    # ```ruby
    # sc = Unicode::UCD.load_sc('17.0.0')
    #
    # sc['Hiragana'].include?(0x3042) # => true
    # sc['Hiragana'].include?(0x30FC) # => false
    # ```
    def load_sc(version)
      sc = {}
      load_file_cp(version, 'Scripts.txt') do |range, script|
        sc[script] ||= RangeSet.new
        sc[script] << range
      end

      sc['Unknown'] = ~sc.values.reduce(RangeSet.new, :|)

      sc
    end

    # Loads `Script_Extensions.txt` from the UCD for the given version, and
    # returns a hash mapping value names of the `Script` property to their code
    # point ranges of which this property value in the `Script_Extensions`
    # property (NOTE: value of the `Script_Extensions` property is a set of
    # `Script` property values).
    #
    # As the same as `load_sc`, each key in the result is a long name for
    # the property value.
    #
    # Most of `Script_Extensions` property values are derived from the `Script`
    # property values. Therefore, this method takes the result of `load_sc` as
    # an argument `sc`. In addition, `Script_Extensions.txt` refers to the short
    # names of the `Script` property values, so this method also takes the result
    # of `load_property_value_aliases` as an argument `sc_aliases`.
    #
    # ```ruby
    # sc = Unicode::UCD.load_sc('17.0.0')
    # _, sc_aliases = Unicode::UCD.load_property_value_aliases('17.0.0')
    # scx = Unicode::UCD.load_scx('17.0.0', sc, sc_aliases)
    #
    # scx['Hiragana'].include?(0x3042) # => true
    # scx['Hiragana'].include?(0x30FC) # => true
    # # U+30FC (KATAKANA-HIRAGANA PROLONGED SOUND MARK; i.e., "ー") has
    # # the `Script` property value `Common`, but it has the `Script_Extensions`
    # # property value `Hiragana` and `Katakana`.
    # ```
    def load_scx(version, sc, sc_aliases)
      scx = {}
      sc.each_key { |script| scx[script] = sc[script].dup }

      common = scx['Common']
      inherited = scx['Inherited']

      load_file_cp(version, 'ScriptExtensions.txt') do |range, scripts|
        scripts.split.each do |script|
          script = sc_aliases[script] || script
          scx[script] << range
        end

        # `Common` or `Inherited`` are not inherited by the Script_Extensions value.
        # See https://www.unicode.org/reports/tr24/.
        range.each do |cp|
          common.delete(cp)
          inherited.delete(cp)
        end
      end

      scx
    end

    # Loads `Blocks.txt` from the UCD for the given version, and returns a hash
    # mapping block names to their code point ranges.
    #
    # ```ruby
    # blocks = Unicode::UCD.load_blocks('17.0.0')
    #
    # blocks['Basic Latin'].include?(0x0041) # => true
    # ```
    def load_blocks(version)
      blocks = {}
      load_file_cp(version, 'Blocks.txt') do |range, block|
        blocks[block] ||= RangeSet.new
        blocks[block] << range
      end

      blocks
    end

    # Loads `DerivedAge.txt` from the UCD for the given version, and returns
    # a hash mapping age values to their code point ranges.
    #
    # ```ruby
    # ages = Unicode::UCD.load_ages('17.0.0')
    #
    # ages['1.1'].include?(0x0041) # => true
    # ages['6.1'].include?(0x1F600) # => true
    # ```
    def load_ages(version)
      ages = {}
      prev_age = nil
      load_file_cp(version, 'DerivedAge.txt') do |range, age|
        if prev_age != age
          # Newer `Age` value overrides older ones, so we need to copy
          # the previous age's range set.
          ages[age] = prev_age ? ages[prev_age].dup : RangeSet.new
          prev_age = age
        end

        ages[age] << range
      end

      ages
    end

    # Loads `PropList.txt` from the UCD for the given version, and returns a hash
    # mapping binary property names to their code point ranges.
    #
    # Note that each key in the result is a long name for the property.
    #
    # ```ruby
    # props = Unicode::UCD.load_props('17.0.0')
    #
    # props['ASCII_Hex_Digit'].include?(0x30) # => true
    # props['ASCII_Hex_Digit'].include?(0x41) # => true
    # props['ASCII_Hex_Digit'].include?(0x42) # => false
    # ```
    def load_props(version)
      props = {}
      load_file_cp(version, 'PropList.txt') do |range, prop|
        props[prop] ||= RangeSet.new
        props[prop] << range
      end

      props
    end

    # Loads `DerivedCoreProperties.txt` from the UCD for the given version, and
    # returns two hashes: the first one maps binary property names to their code
    # point ranges, and the second one maps value names of the `InCB` (`Indic_Conjunct_Break`)
    # property to their code point ranges.
    #
    # Note that each key in the first hash is a long name for the property.
    #
    # ```ruby
    # props, in_cb = Unicode::UCD.load_core_props('17.0.0')
    #
    # props['Alphabetic'].include?(0x0041) # => true
    # in_cb['Consonant'].include?(0x0915) # => true
    # ```
    def load_core_props(version)
      props = {}
      in_cb = {}
      load_file_cp(version, 'DerivedCoreProperties.txt') do |range, prop, value|
        if prop == 'InCB'
          in_cb[value] ||= RangeSet.new
          in_cb[value] << range
        else
          props[prop] ||= RangeSet.new
          props[prop] << range
        end
      end

      in_cb['None'] = ~in_cb.values.reduce(RangeSet.new, :|)

      [props, in_cb]
    end

    # Loads `emoji/emoji-data.txt` from the UCD for the given version, and
    # returns a hash mapping emoji property names to their code point ranges.
    #
    # Note that each key in the result is a long name for the property.
    #
    # ```ruby
    # props = Unicode::UCD.load_emoji_props('17.0.0')
    #
    # props['Emoji'].include?(0x1F600) # => true
    # ```
    def load_emoji_props(version)
      props = {}
      load_file_cp(version, 'emoji/emoji-data.txt') do |range, prop|
        props[prop] ||= RangeSet.new
        props[prop] << range
      end

      props
    end

    # Loads `extracted/DerivedCombiningClass.txt` from the UCD for the given version, and
    # returns a hash mapping combining class values to their code point ranges.
    #
    # Note that the keys in the result are string of decimal numbers.
    #
    # ```ruby
    # ccc = Unicode::UCD.load_ccc('17.0.0')
    #
    # ccc['230'].include?(0x0301) # => true
    # ```
    def load_ccc(version)
      ccc = {}
      load_file_cp(version, 'extracted/DerivedCombiningClass.txt') do |range, ccc_value|
        ccc[ccc_value] ||= RangeSet.new
        ccc[ccc_value] << range
      end

      # The default value of `ccc` is `0`.
      ccc['0'] ||= RangeSet.new
      ccc['0'] = ccc['0'] | ~ccc.except('0').values.reduce(RangeSet.new, :|)

      ccc
    end

    # Loads `auxiliary/GraphemeBreakProperty.txt` from the UCD for the given
    # version, and returns a hash mapping value names of the `Grapheme_Cluster_Break`
    # property to their code point ranges.
    #
    # ```ruby
    # gcb = Unicode::UCD.load_grapheme_cluster_breaks('17.0.0')
    #
    # gcb['CR'].include?(0x000D) # => true
    # ```
    def load_grapheme_cluster_breaks(version)
      gcb = {}
      load_file_cp(version, 'auxiliary/GraphemeBreakProperty.txt') do |range, prop|
        gcb[prop] ||= RangeSet.new
        gcb[prop] << range
      end

      gcb
    end

    # Loads `extracted/DerivedNumericValues.txt` from the UCD for the given version, and
    # returns a hash mapping numeric values to their code point ranges.
    #
    # Note that the keys in the result are `Rational` objects, and they can be used as numbers.
    #
    # ```ruby
    # numeric_values = Unicode::UCD.load_numeric_values('17.0.0')
    #
    # numeric_values[1].include?(0x0031) # => true
    # numeric_values[1/320r].include?(0x11FC0) # => true
    # ```
    def load_numeric_values(version)
      numeric_values = {}
      load_file_cp(version, 'extracted/DerivedNumericValues.txt') do |range, _, _, value|
        value = Rational(value)
        numeric_values[value] ||= RangeSet.new
        numeric_values[value] << range
      end

      numeric_values
    end

    # Loads `PropertyAliases.txt` from the UCD for the given version, and returns
    # a hash mapping property name aliases to their long names.
    #
    # ```ruby
    # prop_aliases = Unicode::UCD.load_prop_aliases('17.0.0')
    #
    # prop_aliases['gc'] # => 'General_Category'
    # prop_aliases['sc'] # => 'Script'
    # ```
    def load_prop_aliases(version)
      prop_aliases = {}
      load_file(version, :ucd, 'PropertyAliases.txt') do |short_name, long_name, *other_names|
        [short_name, *other_names].each { |alias_name| prop_aliases[alias_name] = long_name }
      end

      prop_aliases
    end

    # Loads `PropertyValueAliases.txt` from the UCD for the given version, and
    # returns two hashes: the first one maps value name aliases of the `General_Category`
    # property to their long names, and the second one maps value name aliases of
    # the `Script` property to their long names.
    #
    # ```ruby
    # value_aliases = Unicode::UCD.load_prop_value_aliases('17.0.0')
    #
    # value_aliases['gc']['L'] # => 'Letter'
    # value_aliases['sc']['Hira'] # => 'Hiragana'
    # ```
    def load_prop_value_aliases(version)
      prop_value_aliases = {}

      load_file(version, :ucd, 'PropertyValueAliases.txt') do |prop, short_name, long_name, *other_names|
        value_aliases = prop_value_aliases[prop] ||= {}
        [short_name, *other_names].each do |alias_name|
          value_aliases[alias_name] = long_name
        end
      end

      prop_value_aliases
    end

    # Loads `UnicodeData.txt`, `SpecialCasing.txt`, and `CaseFolding.txt` from
    # the UCD for the given version, and returns a hash mapping code points to
    # their case mapping information.
    #
    # The case mapping information for each code point is represented as a hash
    # with the following keys:
    #
    # - `:upper`: an array of code points that are the uppercase mapping.
    # - `:lower`: an array of code points that are the lowercase mapping.
    # - `:title`: an array of code points that are the titlecase mapping.
    # - `:fold`: an array of code points that are the simple case folding mapping.
    # - `:fold_full`: an array of code points that are the full case folding mapping.
    #
    # By definition, `:fold` should be a single code point, but we represent it
    # as an array for consistency with the other keys.
    #
    # ```ruby
    # case_map = Unicode::UCD.load_case_map('17.0.0')
    #
    # case_map[0x0041]
    # # => {
    #   upper: [0x0041],
    #   lower: [0x0061],
    #   title: [0x0041],
    #   fold: [0x0061],
    #   fold_full: [0x0061]
    # }
    # ```
    def load_case_map(version)
      case_map = {}

      load_file(version, :ucd, 'UnicodeData.txt') do |code, *line|
        next if code.start_with?('<')

        code = code.to_i(16)
        upper, lower, title = line[11..13].map { |v| v.empty? ? nil : v.to_i(16) }

        if upper || lower || title
          case_map[code] = {}
          case_map[code][:upper] = upper ? [upper] : [code]
          case_map[code][:lower] = lower ? [lower] : [code]
          case_map[code][:title] = title ? [title] : [code]
          case_map[code][:fold] = [code]
          case_map[code][:fold_full] = [code]
        end
      end

      load_file(version, :ucd, 'SpecialCasing.txt') do |code, lower, title, upper, conditions|
        next unless conditions.empty?

        code = code.to_i(16)
        lower = lower.split.map { |v| v.to_i(16) }
        title = title.split.map { |v| v.to_i(16) }
        upper = upper.split.map { |v| v.to_i(16) }

        case_map[code] ||= {}
        case_map[code][:lower] = lower == [code] ? [code] : lower
        case_map[code][:title] = title == [code] ? [code] : title
        case_map[code][:upper] = upper == [code] ? [code] : upper
        case_map[code][:fold] = [code]
        case_map[code][:fold_full] = [code]
      end

      load_file(version, :ucd, 'CaseFolding.txt') do |code, type, fold|
        # Ignore Turkic-specific case folding mappings. This support can be done
        # manually.
        next if type == 'T'

        code = code.to_i(16)
        fold = fold.split.map { |v| v.to_i(16) }

        case type
        when 'C'
          case_map[code][:fold] = fold.dup
          case_map[code][:fold_full] = fold
        when 'S'
          case_map[code][:fold] = fold
        when 'F'
          case_map[code][:fold_full] = fold
        end
      end

      case_map
    end

    # Loads emoji sequence data from the Unicode data files for the given version,
    # and returns a hash mapping emoji property names to their code point ranges
    # and sequences.
    #
    # The value for each key in the result is a hash with the following keys:
    #
    # - `:rs`: a `RangeSet` of code points that have the property.
    # - `:seqs`: an array of emoji sequences (each sequence is an array of code points)
    #   that have the property.
    #
    # ```ruby
    # emoji_seqs = Unicode::UCD.load_emoji_seqs('17.0.0')
    #
    # emoji_seqs['Basic_Emoji'][:rs].include?(0x1F600) # => true
    # emoji_seqs['RGI_Emoji'][:seqs].include?([0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467, 0x200D, 0x1F466]) # => true
    # ```
    def load_emoji_seqs(version)
      emoji_seqs = {}

      load_file(version, :emoji, 'emoji-sequences.txt') do |code, prop|
        emoji_seqs[prop] ||= {
          rs: RangeSet.new,
          seqs: [],
        }

        match = code.match(/\A(?<begin>[0-9a-fA-F]+)(?:\.\.(?<end>[0-9a-fA-F]+))?\z/)
        if match
          begin_cp = match[:begin].to_i(16)
          end_cp = match[:end]&.to_i(16) || begin_cp
          emoji_seqs[prop][:rs] << (begin_cp..end_cp)
          next
        end

        emoji_seqs[prop][:seqs] << code.split.map { |v| v.to_i(16) }
      end

      load_file(version, :emoji, 'emoji-zwj-sequences.txt') do |code, prop|
        emoji_seqs[prop] ||= {
          rs: RangeSet.new,
          seqs: [],
        }

        emoji_seqs[prop][:seqs] << code.split.map { |v| v.to_i(16) }
      end

      keys = emoji_seqs.keys
      emoji_seqs['RGI_Emoji'] = {
        rs: keys.map do
          p [it, emoji_seqs[it]]
          emoji_seqs[it][:rs]
        end.reduce(RangeSet.new, :|),
        seqs: keys.flat_map { emoji_seqs[it][:seqs] },
      }

      emoji_seqs
    end

    private

    # Returns a path to the UCD file for the given version and file name.
    def path_for(version, type, file)
      File.join(File.dirname(__FILE__), '../..', 'data/unicode', version, type.to_s, file)
    end

    # Loads the UCD or similar format file for the given version and file name,
    # and yield each line as an array of values.
    def load_file(version, type, file)
      data_path = path_for(version, type, file)

      File.read(data_path).each_line(chomp: true) do |line|
        next if line.start_with?('#') || line.strip.empty?

        line = line.gsub(/#.*\z/, '').split(';', -1).map(&:strip)
        yield(*line)
      end

      nil
    end

    # Loads the UCD file for the given version and file name, and yields each
    # code point range and its associated values.
    def load_file_cp(version, file)
      load_file(version, 'ucd', file) do |range_str, *values|
        match = range_str.match(/\A(?<begin>[0-9a-fA-F]+)(?:\.\.(?<end>[0-9a-fA-F]+))?\z/)

        raise "Invalid range format in #{file}: #{range_str}" unless match

        begin_cp = match[:begin].to_i(16)
        end_cp = match[:end]&.to_i(16) || begin_cp

        yield (begin_cp..end_cp), *values
      end
    end
  end
end
