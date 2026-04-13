# frozen_string_literal: true

require 'test/unit'

require_relative 'case_map'

module Unicode
  class CaseMapTest < Test::Unit::TestCase
    UNICODE_VERSION = '17.0.0'
    CASE_MAP_INSTANCE = Unicode::CaseMap.new(UNICODE_VERSION)

    def setup
      @mapper = CASE_MAP_INSTANCE
    end

    # ===============================================================
    # 1. Tests for `#fold`
    # ===============================================================

    def test_fold_basic
      assert_equal 'abc', @mapper.fold('ABC')
      assert_equal 'abc', @mapper.fold('abc')
    end

    def test_fold_full_vs_simple
      # ß (U+00DF)
      assert_equal 'ss', @mapper.fold("\u00DF", mode: :full)
      assert_equal "\u00DF", @mapper.fold("\u00DF", mode: :simple)
    end

    def test_fold_turkic
      # `fold` against I (U+0049) and İ (U+0130).
      assert_equal 'i', @mapper.fold('I')

      # `fold` with `language: :turkic` against I (U+0049) and İ (U+0130).
      assert_equal "\u0131", @mapper.fold('I', language: :turkic) # ı (without dot)
      assert_equal 'i', @mapper.fold("\u0130", language: :turkic) # i (with dot)
    end

    def test_fold_invalid_args
      assert_raises(ArgumentError) { @mapper.fold('A', mode: :invalid) }
      assert_raises(ArgumentError) { @mapper.fold('A', language: :lithuanian) }
    end

    def test_fold_array_input
      assert_equal [0x0061, 0x0062], @mapper.fold([0x0041, 0x0042])
    end

    # ===============================================================
    # 2. Tests for `#case_map` (basic usage)
    # ===============================================================

    def test_case_map_basic
      assert_equal 'abc', @mapper.case_map(:lower, 'ABC')
      assert_equal 'ABC', @mapper.case_map(:upper, 'abc')
      assert_equal 'Abc', @mapper.case_map(:title, 'aBC')
    end

    def test_case_map_swap
      assert_equal 'aBc', @mapper.case_map(:swap, 'AbC')

      # Swap for special titlecase character (ǲ U+01F2 -> dZ)
      assert_equal 'dZ', @mapper.case_map(:swap, "\u01F2")
      # Special titlecase character swap (ǅ U+01C5 -> dŽ)
      assert_equal "d\u017D", @mapper.case_map(:swap, "\u01C5")
    end

    def test_case_map_invalid_args
      assert_raises(ArgumentError) { @mapper.case_map(:invalid, 'A') }
      assert_raises(ArgumentError) { @mapper.case_map(:lower, 'A', language: :invalid) }
    end

    # ===============================================================
    # 3. Context dependent mappings (final sigma)
    # ===============================================================

    def test_case_map_final_sigma
      # "ΟΣΟΣ" (U+039F U+03A3 U+039F U+03A3) -> "οσος" (U+03BF U+03C3 U+03BF U+03C2)
      input = "\u039F\u03A3\u039F\u03A3"
      expected = "\u03BF\u03C3\u03BF\u03C2"
      assert_equal expected, @mapper.case_map(:lower, input, context: true)

      # Final sigma should be applied even if there is a Case_Ignorable character after it.
      input_with_quote = "\u039F\u03A3'"
      expected_with_quote = "\u03BF\u03C2'"
      assert_equal expected_with_quote, @mapper.case_map(:lower, input_with_quote, context: true)
    end

    # ===============================================================
    # 4. Locale dependent mappings (Turkic languages)
    # ===============================================================

    def test_case_map_turkic_lower
      # I (U+0049) -> ı (U+0131)
      assert_equal "\u0131", @mapper.case_map(:lower, 'I', language: :turkic)

      # İ (U+0130) -> i (U+0069)
      assert_equal 'i', @mapper.case_map(:lower, "\u0130", language: :turkic)

      # I + dot (U+0049 U+0307) -> i (U+0069)
      assert_equal 'i', @mapper.case_map(:lower, "I\u0307", language: :turkic)
    end

    def test_case_map_turkic_upper
      # i (U+0069) -> İ (U+0130)
      assert_equal "\u0130", @mapper.case_map(:upper, 'i', language: :turkic)

      # ı (U+0131) -> I (U+0049)
      assert_equal 'I', @mapper.case_map(:upper, "\u0131", language: :turkic)
    end

    # ===============================================================
    # 5. Locale dependent mappings (Lithuanian)
    # ===============================================================

    def test_case_map_lithuanian_lower
      # I + acute (U+0049 U+0301) -> i + dot + acute (U+0069 U+0307 U+0301)
      assert_equal "i\u0307\u0301", @mapper.case_map(:lower, "I\u0301", language: :lithuanian)

      # Ì (U+00CC) -> i + dot + grave (U+0069 U+0307 U+0300)
      assert_equal "i\u0307\u0300", @mapper.case_map(:lower, "\u00CC", language: :lithuanian)
    end

    def test_case_map_lithuanian_upper
      # i + dot + acute (U+0069 U+0307 U+0301) -> I + acute (U+0049 U+0301)
      # NOTE: The U+0307 (combining dot above) is removed in the uppercase mapping for Lithuanian.
      assert_equal "I\u0301", @mapper.case_map(:upper, "i\u0307\u0301", language: :lithuanian)
    end
  end
end
