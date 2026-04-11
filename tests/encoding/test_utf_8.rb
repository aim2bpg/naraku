module Encoding
  class Test_UTF_8 < Mtest::Test
    E = Naraku::Encoding::UTF_8

    def test_name
      assert_equal 'UTF-8', E.name
    end

    def test_min_mbc_width
      assert_equal 1, E.min_mbc_width
    end

    def test_max_mbc_width
      assert_equal 4, E.max_mbc_width
    end

    def test_single_byte_threshold
      assert_equal 0x80, E.single_byte_threshold
    end

    def test_self_sync
      assert E.self_sync?
    end

    def test_unicode
      assert E.unicode?
    end

    def test_scan_mbc_width
      (0x00..0x7F).each do |code|
        assert_equal 1, E.scan_mbc_width(code.chr)
      end

      (0x80..0xC1).each do |code|
        assert_equal 0, E.scan_mbc_width(code.chr)
      end

      (0xC2..0xDF).each do |code|
        assert_equal(-1, E.scan_mbc_width(code.chr))
      end

      (0xE0..0xEF).each do |code|
        assert_equal(-2, E.scan_mbc_width(code.chr))
      end

      (0xF0..0xF4).each do |code|
        assert_equal(-3, E.scan_mbc_width(code.chr))
      end

      (0xF5..0xFF).each do |code|
        assert_equal 0, E.scan_mbc_width(code.chr)
      end

      assert_equal 2, E.scan_mbc_width("\xC3\x9F") # "ß"
      assert_equal 3, E.scan_mbc_width("\xE3\x81\x82") # "あ"
      assert_equal(-1, E.scan_mbc_width("\xE3\x81")) # "あ" (incomplete)
      assert_equal 4, E.scan_mbc_width("\xF0\x9F\x98\x80") # "😀"
      assert_equal(-1, E.scan_mbc_width("\xF0\x9F\x98")) # "😀" (incomplete)
      assert_equal(-2, E.scan_mbc_width("\xF0\x9F")) # "😀" (incomplete)
    end

    def test_encode_mbc
      (0x00..0x7F).each do |code|
        assert_equal code.chr, E.encode_mbc(code)
      end

      assert_equal "\xC3\x9F", E.encode_mbc(0xDF) # "ß"
      assert_equal "\xE3\x81\x82", E.encode_mbc(0x3042) # "あ"
      assert_equal "\xF0\x9F\x98\x80", E.encode_mbc(0x1F600) # "😀"

      (0xD800..0xDFFF).each do |code| # surrogate code points
        assert_raises(Naraku::Error, '') do
          E.encode_mbc(code)
        end
      end

      assert_raises(Naraku::Error, '') {
        E.encode_mbc(0x110000)
      }
    end

    def test_decode_mbc
      (0x00..0x7F).each do |code|
        assert_equal code, E.decode_mbc(code.chr)
      end

      assert_equal 0xDF, E.decode_mbc("\xC3\x9F") # "ß"
      assert_equal 0x3042, E.decode_mbc("\xE3\x81\x82") # "あ"
      assert_equal 0x1F600, E.decode_mbc("\xF0\x9F\x98\x80") # "😀"
    end

    def test_adjust_mbc_head
      context = Naraku::Encoding::AdjustMbcHeadContext.new(
        "ABC\xC3\x9F\xE3\x81\x82\xF0\x9F\x98\x80",
        false
      )
      [0, 1, 2, 3, 3, 5, 5, 5, 8, 8, 8, 8].each_with_index do |expected, index|
        assert_equal expected, E.adjust_mbc_head(index, context)
      end
    end

    def test_self_sync_string
      assert E.self_sync_string?("ABC")
      assert E.self_sync_string?("\xC3\x9F")
      assert E.self_sync_string?("\xE3\x81\x82")
      assert E.self_sync_string?("\xF0\x9F\x98\x80")
    end

    def test_case_fold
      assert_equal [0x31], E.case_fold(0x31)
      assert_equal [0x61], E.case_fold(0x61)
      assert_equal [0x61], E.case_fold(0x41)
      assert_equal [0x61], E.case_fold(0x41, :full)
      assert_equal [0x7A], E.case_fold(0x5A)
      assert_equal [0x7A], E.case_fold(0x5A, :full)

      # "ß" (0xDF) => "ss" (0x73 0x73)
      assert_equal [0xDF], E.case_fold(0xDF)
      assert_equal [0x73, 0x73], E.case_fold(0xDF, :full)

      # "ẞ" (0x1E9E) => "ß" (0xDF) / "ss" (0x73 0x73)
      assert_equal [0xDF], E.case_fold(0x1E9E)
      assert_equal [0x73, 0x73], E.case_fold(0x1E9E, :full)

      # Turkish/Azeri-specific case folding:
      # "İ" (0x130) => "i" (0x69) + "◌̇" (0x307)
      assert_equal [0x130], E.case_fold(0x130)
      assert_equal [0x69, 0x307], E.case_fold(0x130, :full)
      assert_equal [0x69], E.case_fold(0x130, :turkish_azeri)
      # "I" (0x49) => "i" (0x69) / "ı" (0x131)
      assert_equal [0x69], E.case_fold(0x49)
      assert_equal [0x131], E.case_fold(0x49, :turkish_azeri)
    end

    def test_expand_case_unfold
      assert_equal nil, E.expand_case_unfold([0x31])
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x41 }], E.expand_case_unfold([0x61])
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x41 }], E.expand_case_unfold([0x61], :full)
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x5A }], E.expand_case_unfold([0x7A])
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x5A }], E.expand_case_unfold([0x7A], :full)

      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x1E9E }], E.expand_case_unfold([0xDF])
      assert_equal [
        { folded_codes_len: 1, unfolded_code: 0x53 },
        { folded_codes_len: 1, unfolded_code: 0x17F },
        { folded_codes_len: 2, unfolded_code: 0xDF },
        { folded_codes_len: 2, unfolded_code: 0x1E9E },
      ], E.expand_case_unfold([0x73, 0x73], :full)

      assert_equal [
        { folded_codes_len: 1, unfolded_code: 0x49 },
        { folded_codes_len: 2, unfolded_code: 0x130 },
      ], E.expand_case_unfold([0x69, 0x307], :full)
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x130 }], E.expand_case_unfold([0x69], :turkish_azeri)
    end

    def test_iterate_case_fold
      E.iterate_case_fold do |from_code, to_codes|
        assert_equal to_codes, E.case_fold(from_code)
      end

      E.iterate_case_fold(:full) do |from_code, to_codes|
        assert_equal to_codes, E.case_fold(from_code, :full)
      end

      E.iterate_case_fold(:turkish_azeri) do |from_code, to_codes|
        assert_equal to_codes, E.case_fold(from_code, :turkish_azeri)
      end
    end

    def test_cprop
      (0x00..0x7F).each do |code|
        assert E.cprop?(code, 'ASCII')
      end

      [*(0x41..0x5A), *(0x61..0x7A)].each do |code|
        assert E.cprop?(code, 'Alpha')
      end

      assert E.cprop?(0x3042, 'Script=Hira') # "あ"
      assert !E.cprop?(0x30FC, 'Script=Hira') # "ー"

      assert E.cprop?(0x3042, 'Script_Extensions=Hira') # "あ"
      assert E.cprop?(0x30FC, 'Script_Extensions=Hira') # "ー"
    end

    def test_cprop_code_range
      assert_equal [0x00..0x7F], E.cprop_code_range('ASCII')

      alpha_code_range = E.cprop_code_range('Alpha')
      [0x41..0x5A, 0x61..0x7A, 0x3041..0x3096].each do |range|
        assert alpha_code_range.include?(range)
      end
    end
  end
end