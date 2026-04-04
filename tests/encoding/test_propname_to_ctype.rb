module Encoding
  class TestPropnameToCtype < Mtest::Test
    def test_ignore_case
      ctype = Naraku::Encoding.propname_to_ctype('NEWLINE')
      assert_equal ctype, Naraku::Encoding.propname_to_ctype('newline')
      assert_equal ctype, Naraku::Encoding.propname_to_ctype('NewLine')
      assert_equal ctype, Naraku::Encoding.propname_to_ctype('nEWlINE')
    end

    def test_ignore_chars
      ctype = Naraku::Encoding.propname_to_ctype('NEWLINE')
      assert_equal ctype, Naraku::Encoding.propname_to_ctype('NEW LINE')
      assert_equal ctype, Naraku::Encoding.propname_to_ctype('NEW_LINE')
      assert_equal ctype, Naraku::Encoding.propname_to_ctype('NEW-LINE')
      assert_equal ctype, Naraku::Encoding.propname_to_ctype('  N-E-W__L-I-N-E  ')
    end

    def test_non_standard_property
      %w[
        NEWLINE Alpha Blank Cntrl Digit Graph Lower Print XPosixPunct Space
        Upper XDigit Word Alnum ASCII Punct
        Any Assigned
      ].each_with_index do |name, ctype|
        assert_equal ctype, Naraku::Encoding.propname_to_ctype(name)
      end
    end

    def test_script
      [
        %w[Latin Latn],
        %w[Hiragana Hira],
        %w[Katakana Kana],
        %w[Common Zyyy],
        %w[Inherited Zinh],
        %w[Unknown Zzzz],
      ].each do |(long_name, short_name)|
        ctype = Naraku::Encoding.propname_to_ctype("Script=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype(short_name)
        assert_equal ctype, Naraku::Encoding.propname_to_ctype(long_name)
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("sc=#{short_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("sc=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("Script=#{short_name}")
      end
    end

    def test_script_extensions
      [
        %w[Latin Latn],
        %w[Hiragana Hira],
        %w[Katakana Kana],
        %w[Common Zyyy],
        %w[Inherited Zinh],
        %w[Unknown Zzzz],
      ].each do |(long_name, short_name)|
        ctype = Naraku::Encoding.propname_to_ctype("Script_Extensions=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("Script_Extensions=#{short_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("Script_Extensions=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("scx=#{short_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("scx=#{long_name}")
      end
    end

    def test_general_category
      [
        %w[Lu Uppercase_Letter],
        %w[Ll Lowercase_Letter],
        %w[Lt Titlecase_Letter],
        %w[L Letter],
        %w[LC Cased_Letter],
        %w[M Mark Combining_Mark],
        %w[Mn Nonspacing_Mark],
        %w[Sc Currency_Symbol],
        %w[Zs Space_Separator],
        %w[Nd Decimal_Number],
      ].each do |(short_name, long_name, other_name)|
        ctype = Naraku::Encoding.propname_to_ctype("General_Category=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype(short_name)
        assert_equal ctype, Naraku::Encoding.propname_to_ctype(long_name)
        assert_equal ctype, Naraku::Encoding.propname_to_ctype(other_name) if other_name
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("General_Category=#{short_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("General_Category=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("General_Category=#{other_name}") if other_name
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("gc=#{short_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("gc=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("gc=#{other_name}") if other_name
      end
    end

    def test_block
      [
        %w[Basic_Latin ASCII],
        %w[Kana_Extended_A Kana_Ext_A],
      ].each do |(long_name, short_name)|
        ctype = Naraku::Encoding.propname_to_ctype("Block=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("In_#{short_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("In_#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("Block=#{short_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("Block=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("blk=#{short_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("blk=#{long_name}")
      end
    end

    def test_age
      assert Naraku::Encoding.propname_to_ctype('Age=1.1')
      assert Naraku::Encoding.propname_to_ctype('Age=2.0')
      assert Naraku::Encoding.propname_to_ctype('Age=17.0')
    end

    def test_gcb
      [
        %w[Control CN],
        %w[Extend EX],
      ].each do |(long_name, short_name)|
        ctype = Naraku::Encoding.propname_to_ctype("Grapheme_Cluster_Break=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("Grapheme_Cluster_Break=#{short_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("gcb=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("gcb=#{short_name}")
      end
    end

    def test_ccc
      [
        %w[0 Not_Reordered NR],
        %w[1 Overlay OV],
        %w[220 Below B],
      ].each do |(num_value, long_name, short_name)|
        ctype = Naraku::Encoding.propname_to_ctype("Canonical_Combining_Class=#{num_value}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("Canonical_Combining_Class=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("Canonical_Combining_Class=#{short_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("ccc=#{num_value}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("ccc=#{long_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("ccc=#{short_name}")
      end
    end

    def test_prop
      [
        %w[ASCII_Hex_Digit AHex],
        %w[White_Space WSpace],
        %w[Bidi_Control Bidi_C],
        %w[Hex_Digit Hex],
        %w[Other_Math OMath],
      ].each do |(long_name, short_name)|
        ctype = Naraku::Encoding.propname_to_ctype(long_name)
        assert_equal ctype, Naraku::Encoding.propname_to_ctype(short_name)
      end
    end

    def test_core_prop
      [
        %w[Math Math],
        %w[Cased Cased],
        %w[Case_Ignorable CI],
      ].each do |(long_name, short_name)|
        ctype = Naraku::Encoding.propname_to_ctype(long_name)
        assert_equal ctype, Naraku::Encoding.propname_to_ctype(short_name)
      end
    end

    def test_in_cb
      %w[Consonant Extend Linker None].each do |value_name|
        ctype = Naraku::Encoding.propname_to_ctype("Indic_Conjunct_Break=#{value_name}")
        assert_equal ctype, Naraku::Encoding.propname_to_ctype("in_cb=#{value_name}")
      end
    end

    def test_emoji_prop
      [
        %w[Emoji Emoji],
        %w[Emoji_Modifier EMod],
        %w[Emoji_Modifier_Base EBase],
      ].each do |(long_name, short_name)|
        ctype = Naraku::Encoding.propname_to_ctype(long_name)
        assert_equal ctype, Naraku::Encoding.propname_to_ctype(short_name)
      end
    end
  end
end