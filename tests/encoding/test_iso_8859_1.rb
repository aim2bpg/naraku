module Encoding
  class Test_ASCII_8BIT < Mtest::Test
    E = Naraku::Encoding::ISO_8859_1

    def test_name
      assert_equal 'ISO-8859-1', E.name
    end

    def test_min_mbc_width
      assert_equal 1, E.min_mbc_width
    end

    def test_max_mbc_width
      assert_equal 1, E.max_mbc_width
    end

    def test_single_byte_threshold
      assert_equal 0x100, E.single_byte_threshold
    end

    def test_self_sync
      assert E.self_sync?
    end

    def test_unicode
      assert !E.unicode?
    end

    def test_scan_mbc_width
      (0x00..0xFF).each do |code|
        assert_equal 1, E.scan_mbc_width(code.chr)
      end
    end

    def test_encode_mbc
      (0x00..0xFF).each do |code|
        assert_equal code.chr, E.encode_mbc(code)
      end
    end

    def test_decode_mbc
      (0x00..0xFF).each do |code|
        assert_equal code, E.decode_mbc(code.chr)
      end
    end

    def test_adjust_mbc_head
      context = Naraku::Encoding::AdjustMbcHeadContext.new("abcd", false)
      (0..4).each do |index|
        assert_equal index, E.adjust_mbc_head(index, context)
      end
    end

    def test_self_sync_string
      assert E.self_sync_string?("abc")
    end

    def test_case_fold
      assert_equal [0x31], E.case_fold(0x31)
      assert_equal [0x61], E.case_fold(0x61)
      assert_equal [0x61], E.case_fold(0x41)
      assert_equal [0x61], E.case_fold(0x41, :full)
      assert_equal [0x7A], E.case_fold(0x5A)
      assert_equal [0x7A], E.case_fold(0x5A, :full)
      assert_equal [0xE6], E.case_fold(0xC6)
      assert_equal [0xDF], E.case_fold(0xDF)
      assert_equal [0x73, 0x73], E.case_fold(0xDF, :full)
    end

    def test_expand_case_unfold
      assert_equal nil, E.expand_case_unfold([0x31])
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x41 }], E.expand_case_unfold([0x61])
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x41 }], E.expand_case_unfold([0x61], :full)
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x5A }], E.expand_case_unfold([0x7A])
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x5A }], E.expand_case_unfold([0x7A], :full)
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0xC6 }], E.expand_case_unfold([0xE6])
      assert_equal nil, E.expand_case_unfold([0xDF])
      assert_equal [
        { folded_codes_len: 1, unfolded_code: 0x53 },
        { folded_codes_len: 2, unfolded_code: 0xDF },
      ], E.expand_case_unfold([0x73, 0x73], :full)
    end

    def test_iterate_case_fold
      E.iterate_case_fold do |from_code, to_codes|
        assert_equal to_codes, E.case_fold(from_code)
      end

      E.iterate_case_fold(:full) do |from_code, to_codes|
        assert_equal to_codes, E.case_fold(from_code, :full)
      end
    end

    def test_cprop
      (0x00..0x7F).each do |code|
        assert E.cprop?(code, 'ASCII')
      end

      [*(0x41..0x5A), *(0x61..0x7A), 0xAA, 0xB5, 0xBA, *(0xC0..0xD6), *(0xD8..0xF6), *(0xF8..0xFF)].each do |code|
        assert E.cprop?(code, 'Alpha')
      end
    end

    def test_cprop_code_range
      assert_equal [0x00..0x7F], E.cprop_code_range('ASCII')

      assert_equal [0x41..0x5A, 0x61..0x7A, 0xAA..0xAA, 0xB5..0xB5, 0xBA..0xBA, 0xC0..0xD6, 0xD8..0xF6, 0xF8..0xFF], E.cprop_code_range('Alpha')
    end
  end
end
