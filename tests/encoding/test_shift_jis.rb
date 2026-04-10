module Encoding
  class Test_Shift_JIS < Mtest::Test
    E = Naraku::Encoding::SHIFT_JIS

    def test_name
      assert_equal 'Shift_JIS', E.name
    end

    def test_min_mbc_width
      assert_equal 1, E.min_mbc_width
    end

    def test_max_mbc_width
      assert_equal 2, E.max_mbc_width
    end

    def test_single_byte_threshold
      assert_equal 0x80, E.single_byte_threshold
    end

    def test_self_sync
      assert !E.self_sync?
    end

    def test_unicode
      assert !E.unicode?
    end

    def test_scan_mbc_width
      (0x00..0x7F).each do |code|
        assert_equal 1, E.scan_mbc_width(code.chr)
      end

      assert_equal 0, E.scan_mbc_width(0x80.chr)

      (0x81..0x9F).each do |code|
        assert_equal(-1, E.scan_mbc_width(code.chr))
      end

      assert_equal 0, E.scan_mbc_width(0xA0.chr)

      (0xA1..0xDF).each do |code|
        assert_equal 1, E.scan_mbc_width(code.chr)
      end

      (0xE0..0xFC).each do |code|
        assert_equal(-1, E.scan_mbc_width(code.chr))
      end

      (0xFD..0xFF).each do |code|
        assert_equal 0, E.scan_mbc_width(code.chr)
      end

      assert_equal 2, E.scan_mbc_width("\x81\x40")
      assert_equal 2, E.scan_mbc_width("\x81\x81")
      assert_equal 2, E.scan_mbc_width("\xFC\xFC")

      assert_equal 0, E.scan_mbc_width("\x81\x00")
    end

    def test_encode_mbc
      (0x00..0x7F).each do |code|
        assert_equal code.chr, E.encode_mbc(code)
      end

      (0xA1..0xDF).each do |code|
        assert_equal code.chr, E.encode_mbc(code)
      end

      assert_equal "\x81\x40", E.encode_mbc(0x8140)
      assert_equal "\x81\x81", E.encode_mbc(0x8181)
      assert_equal "\xFC\xFC", E.encode_mbc(0xFCFC)
    end

    def test_decode_mbc
      (0x00..0x7F).each do |code|
        assert_equal code, E.decode_mbc(code.chr)
      end

      (0xA1..0xDF).each do |code|
        assert_equal code, E.decode_mbc(code.chr)
      end

      assert_equal 0x8140, E.decode_mbc("\x81\x40")
      assert_equal 0x8181, E.decode_mbc("\x81\x81")
      assert_equal 0xFCFC, E.decode_mbc("\xFC\xFC")
    end

    def test_adjust_mbc_head
      [false, true].each do |use_cache|
        context = Naraku::Encoding::AdjustMbcHeadContext.new(
          # "あいうえおABCか0き1く2け3こ4" + "＝" * 100
          "\x82\xA0\x82\xA2\x82\xA4\x82\xA6\x82\xA8ABC\x82\xA90\x82\xAB1\x82\xAD2\x82\xAF3\x82\xB14" + "\x81\x81" * 100,
          use_cache
        )
        expected_results = [
          0, 0, 2, 2, 4, 4, 6, 6, 8, 8,
          10, 11, 12,
          13, 13, 15, 16, 16, 18, 19, 19, 21, 22, 22, 24, 25, 25, 27,
        ] + 100.times.flat_map { |i| [28 + i * 2, 28 + i * 2] }.to_a
        expected_results.each_with_index do |expected, offset|
          assert_equal expected, E.adjust_mbc_head(offset, context)
        end
      end
    end

    def test_adjust_mbc_head_cache_effectiveness
      n = 100_000
      context = Naraku::Encoding::AdjustMbcHeadContext.new(
        "\x81\x81" * n, # "＝" * n
        true            # use_cache: true
      )

      # If `use_cache = false`, it takes 10 seconds in my environment
      # (MacBook Pro M1, 14-inch, 2021; 16GB RAM).

      assert_timeout(1.0) do
        (n - 1).downto(0) do |i|
          assert_equal i * 2, E.adjust_mbc_head(i * 2 + 1, context)
          assert_equal i * 2, E.adjust_mbc_head(i * 2, context)
        end
      end
    end

    def test_self_sync_string
      assert E.self_sync_string?("123")
      assert !E.self_sync_string?("ABC")
      assert !E.self_sync_string?("\x81\x81") # "＝"
    end

    def test_case_fold
      assert_equal [0x31], E.case_fold(0x31)
      assert_equal [0x61], E.case_fold(0x61)
      assert_equal [0x61], E.case_fold(0x41)
      assert_equal [0x61], E.case_fold(0x41, :full)
      assert_equal [0x7A], E.case_fold(0x5A)
      assert_equal [0x7A], E.case_fold(0x5A, :full)

      # "Ａ" (0x8260) => "ａ" (0x8281)
      assert_equal [0x8281], E.case_fold(0x8260)
      assert_equal [0x8281], E.case_fold(0x8260, :full)
      # "Л" (0x844C) => "л" (0x847C)
      assert_equal [0x847C], E.case_fold(0x844C)
      assert_equal [0x847C], E.case_fold(0x844C, :full)
    end

    def test_expand_case_fold
      assert_equal nil, E.expand_case_unfold([0x31])
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x41 }], E.expand_case_unfold([0x61])
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x41 }], E.expand_case_unfold([0x61], :full)
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x5A }], E.expand_case_unfold([0x7A])
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x5A }], E.expand_case_unfold([0x7A], :full)

      # "Ａ" (0x8260) => "ａ" (0x8281)
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x8260 }], E.expand_case_unfold([0x8281])
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x8260 }], E.expand_case_unfold([0x8281], :full)
      # "Л" (0x844C) => "л" (0x847C)
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x844C }], E.expand_case_unfold([0x847C])
      assert_equal [{ folded_codes_len: 1, unfolded_code: 0x844C }], E.expand_case_unfold([0x847C], :full)
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

      [*(0x41..0x5A), *(0x61..0x7A), *(0x8260..0x8279), *(0x8281..0x829A)].each do |code|
        assert E.cprop?(code, 'Alpha')
      end

      assert E.cprop?(0x88EA, 'Script=Han') # "一"
      assert E.cprop?(0x8B43, 'Script=Han') # "気"
      assert E.cprop?(0x92CA, 'Script=Han') # "通"
      assert E.cprop?(0x8AD1, 'Script=Han') # "貫"
      assert !E.cprop?(0x82A0, 'Script=Han') # "あ"
    end

    def test_cprop_code_range
      assert_equal [0x00..0x7F], E.cprop_code_range('ASCII')

      alpha_code_range = E.cprop_code_range('Alpha')
      [0x41..0x5A, 0x61..0x7A, 0x8260..0x8279, 0x8281..0x829A, 0x829F..0x82F1].each do |range|
        assert alpha_code_range.include?(range)
      end
    end
  end
end