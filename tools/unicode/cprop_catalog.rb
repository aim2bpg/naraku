require_relative 'ucd'

# Supported Unicode properties:
#
# - General_Category
# - Script
# - Script_Extensions
# - Age
# - Block
# - Grapheme_Cluster_Break
# - Canonical_Combining_Class
# - properties defined in PropList.txt (e.g., White_Space, Bidi_Control, etc.)
# - Indic_Conjunct_Break (contained in PropList.txt)
# - propertied defined in DerivedCoreProperties.txt (e.g., Math, Alphabetic, etc.)
# - properties defined in emoji/emoji-data.txt (e.g., Emoji, Emoji_Presentation, etc.)

module Unicode
  class CpropCatalog
    # A list of names of character properties that are supported by Onigmo but
    # are not defined in the UCD.
    #
    # `XPosixPunct` is a non-standard character property that matches not only Unicode
    # punctuation characters, but also punctuation characters defined in the POSIX
    # standard.
    NON_STANDARD_CPROP_NAMES = %w[
      NEWLINE Alpha Blank Cntrl Digit Graph Lower Print XPosixPunct Space Upper XDigit Word Alnum ASCII Punct
      Any Assigned
    ].freeze

    def self.normalize_name(name)
      # This follows the normalization rules for property and value names in UTS #18.
      #
      # > Matching of Binary, Enumerated, Catalog, and Name values must follow the
      # > Matching Rules from [UAX44] with one exception: implementations are not
      # > required to ignore an initial prefix string of "is" in property values.
      #
      # The "Matching Rules from [UAX44]" refers to the following normalization steps:
      #
      # > Ignore case, whitespace, underscore ('_'), hyphens, and any initial prefix
      # > string "is".
      #
      # See https://www.unicode.org/reports/tr18/#RL1.2 and https://www.unicode.org/reports/tr44/#UAX44-LM3.

      name.downcase(:fold).gsub(/[- _]+/, '')
    end

    Cprop = Data.define(
      :name,
      :id,
      :category,
      :range_set,
      :prop_name,
      :prop_name_aliases,
      :value_name,
      :value_name_aliases,
      :other_names
    )

    class Cprop
      def constant_name
        name.upcase.gsub(/[- =.]+/, '_')
      end

      def possible_names
        return [name] + prop_name_aliases + other_names unless value_name

        names = [prop_name, *prop_name_aliases].flat_map do |prop_name|
          [value_name, *value_name_aliases].map do |value_name|
            "#{prop_name}=#{value_name}"
          end
        end

        names + other_names
      end
    end

    def initialize(version)
      @version = version

      @prop_aliases = UCD.load_prop_aliases(version)
      @prop_value_aliases = UCD.load_prop_value_aliases(version)

      @prop_aliases_inv = {}
      @prop_aliases.each do |alias_name, long_name|
        @prop_aliases_inv[long_name] ||= []
        @prop_aliases_inv[long_name] << alias_name
      end

      @prop_value_aliases_inv = {}
      @prop_value_aliases.each do |short_prop_name, value_aliases|
        long_prop_name = @prop_aliases[short_prop_name] || short_prop_name
        value_aliases_inv = @prop_value_aliases_inv[long_prop_name] ||= {}
        value_aliases.each do |alias_name, long_name|
          value_aliases_inv[long_name] ||= []
          value_aliases_inv[long_name] << alias_name
        end
      end

      @gc = UCD.load_gc(version)
      @sc = UCD.load_sc(version)
      @scx = UCD.load_scx(version, @sc, @prop_value_aliases['sc'])
      @blocks = UCD.load_blocks(version)
      @ages = UCD.load_ages(version)
      @gcb = UCD.load_grapheme_cluster_breaks(version)
      @ccc = UCD.load_ccc(version)
      @props = UCD.load_props(version)
      @core_props, @in_cb = UCD.load_core_props(version)
      @emoji_props = UCD.load_emoji_props(version)

      @non_standards = setup_non_standards

      @id = 0
      @cprops = []
      @max_default_support_cprop_id = 0

      setup_cprops
    end

    attr_reader :cprops, :max_default_support_cprop_id

    def setup_non_standards
      non_standards = {}

      non_standards['Any'] = RangeSet.new(0x00..0x10FFFF)
      non_standards['Assigned'] = RangeSet.new(0x00..0x10FFFF) - @gc['Cn']

      non_standards['NEWLINE'] = RangeSet.new(0x0A)
      non_standards['Alpha'] = @core_props['Alphabetic']
      non_standards['Blank'] = @gc['Zs'] | RangeSet.new(0x09)
      non_standards['Cntrl'] = @gc['Cc']
      non_standards['Digit'] = @gc['Nd']
      non_standards['Graph'] = non_standards['Any'] - @gc['Z'] - @gc['Cc'] - @gc['Cs'] - @gc['Cn']
      non_standards['Lower'] = @core_props['Lowercase']
      non_standards['Print'] = non_standards['Graph'] | @gc['Zs']
      non_standards['XPosixPunct'] = @gc['P'] | RangeSet.new(0x24, 0x2b, 0x3c, 0x3d, 0x3e, 0x5e, 0x60, 0x7c, 0x7e)
      non_standards['Space'] = @props['White_Space']
      non_standards['Upper'] = @core_props['Uppercase']

      # NOTE(makenowjust): In the Onigmo implementation, this value is explicitly given.
      # However, this value is just the same as the `ASCII_Hex_Digit` property defined in
      # the UCD, so we can just use that instead.
      non_standards['XDigit'] = @props['ASCII_Hex_Digit']

      non_standards['Word'] = non_standards['Alpha'] | @gc['M'] | non_standards['Digit'] | @gc['Pc'] | @props['Join_Control']
      non_standards['Alnum'] = non_standards['Alpha'] | non_standards['Digit']

      # NOTE(makenowjust): This value is also explicitly given in the Onigmo implementation,
      # but it is just the same as the `Basic Latin` block (actually, it is aliased with `ASCII`),
      # so we can just use that instead.
      non_standards['ASCII'] = @blocks['Basic Latin']

      non_standards['Punct'] = @gc['P']

      non_standards
    end

    def setup_cprops
      NON_STANDARD_CPROP_NAMES.each do |name|
        range_set = @non_standards[name]
        add_cprop(
          name:,
          category: 'Non Standard Character Properties',
          range_set:,
          prop_name: name
        )
      end

      @sc.each do |value_name, range_set|
        add_cprop_sc(value_name, range_set)
      end

      @max_default_support_cprop_id = @id - 1

      @scx.each do |value_name, range_set|
        add_cprop_pv('Script_Extensions', value_name, range_set)
      end

      @gc.each do |short_value_name, range_set|
        add_cprop_gc(short_value_name, range_set)
      end

      @blocks.each do |value_name, range_set|
        value_name = value_name.gsub(/[ -]/, '_')
        names = [value_name]
        names += @prop_value_aliases_inv['Block'][value_name] || []
        names.map! { CpropCatalog.normalize_name(it) }.uniq!
        add_cprop_pv('Block', value_name, range_set, names.map { "In_#{it}" })
      end

      @ages.each do |value_name, range_set|
        add_cprop_pv('Age', value_name, range_set)
      end

      @gcb.each do |value_name, range_set|
        add_cprop_pv('Grapheme_Cluster_Break', value_name, range_set)
      end

      @ccc.each do |value, range_set|
        value_name = @prop_value_aliases['ccc'][value] || value
        add_cprop_pv('Canonical_Combining_Class', value_name, range_set)
      end

      @props.each do |prop_name, range_set|
        add_cprop_p(prop_name, range_set, category: 'Binary Properties')
      end

      @core_props.each do |prop_name, range_set|
        add_cprop_p(prop_name, range_set, category: 'Derived Core Properties')
      end

      @in_cb.each do |value_name, range_set|
        add_cprop_pv('Indic_Conjunct_Break', value_name, range_set, category: 'Derived Core Properties')
      end

      @emoji_props.each do |prop_name, range_set|
        add_cprop_p(prop_name, range_set, category: 'Emoji Properties')
      end
    end

    def setup_cprops_non_standard
      NON_STANDARD_CPROP_NAMES.each do |name|
        range_set = @non_standards[name]
        add_cprop(
          name:,
          range_set:,
          prop_name: name
        )
      end
    end

    def add_cprop_gc(short_value_name, range_set)
      value_name = @prop_value_aliases['gc'][short_value_name] || short_value_name
      value_name_aliases = @prop_value_aliases_inv['General_Category'][value_name] || []
      value_name_aliases << value_name
      # These aliases are conflict with non-standard character types, so we need to exclude them.
      value_name_aliases -= %w[digit cntrl punct]
      add_cprop_pv('General_Category', value_name, range_set, value_name_aliases)
    end

    def add_cprop_sc(value_name, range_set)
      value_name_aliases = @prop_value_aliases_inv['Script'][value_name] || []
      value_name_aliases << value_name
      value_name_aliases.uniq!
      add_cprop_pv('Script', value_name, range_set, value_name_aliases)
    end

    def add_cprop_pv(prop_name, value_name, range_set, other_names = [], category: prop_name)
      prop_name_aliases = @prop_aliases_inv[prop_name] || []
      value_name_aliases = @prop_value_aliases_inv[prop_name == 'Script_Extensions' ? 'Script' : prop_name][value_name] || []
      add_cprop(
        name: "#{prop_name}=#{value_name}",
        category:,
        range_set:,
        prop_name:,
        prop_name_aliases:,
        value_name:,
        value_name_aliases:,
        other_names:
      )
    end

    def add_cprop_p(prop_name, range_set, category:)
      prop_name_aliases = @prop_aliases_inv[prop_name] || []
      prop_name_aliases -= %w[space Alpha Lower Upper]
      add_cprop(
        name: prop_name,
        category:,
        range_set:,
        prop_name:,
        prop_name_aliases:,
        value_name: nil,
        value_name_aliases: [],
        other_names: []
      )
    end

    def add_cprop(name:, category:, range_set:, prop_name:, prop_name_aliases: [], value_name: nil, value_name_aliases: [], other_names: [])
      id = @id
      prop_name, *prop_name_aliases = [prop_name, *prop_name_aliases].uniq { CpropCatalog.normalize_name(it) }
      value_name, *value_name_aliases = [value_name, *value_name_aliases].uniq { CpropCatalog.normalize_name(it) }
      @cprops << Cprop.new(
        name:,
        id:,
        category:,
        range_set:,
        prop_name:,
        prop_name_aliases:,
        value_name:,
        value_name_aliases:,
        other_names:
      )
      @id += 1
      id
    end
  end
end
