  class PostprocessTest < Mtest::Test
    def assert_postprocess_error(pattern, message, offset:, length:)
      parser = Naraku::Parser.new(Naraku::Encoding::UTF_8, pattern)
      root = parser.parse
      begin
        parser.postprocess(root)
      rescue Naraku::ParseError => ex
        expected_message =
          if length > 0
            "#{message} (at span #{offset}...#{offset + length})"
          else
            "#{message} (at offset #{offset})"
          end
        assert_equal expected_message, ex.message.lines.first.chomp
        assert_equal offset, ex.offset
        assert_equal length, ex.length
        return
      end

      assert false, "Expected Naraku::ParseError to be raised for #{pattern.inspect}"
    end

    def collect_postprocess_warnings(pattern, encoding: Naraku::Encoding::UTF_8, **options)
      warnings = []
      parser = Naraku::Parser.new(
        encoding,
        pattern,
        warning_func: ->(warning, offset, length) {
          warnings << {
            warning: warning,
            message: Naraku.warning_message(warning),
            offset: offset,
            length: length,
          }
        },
        **options
      )
      root = parser.parse
      parser.postprocess(root)
      warnings
    end

    def test_named_capture_are_renumbered_and_numbered_capture_becomes_group
      parser = Naraku::Parser.new(Naraku::Encoding::UTF_8, '(?<a>x)(y)(?<b>z)')
      root = parser.parse
      parser.postprocess(root)
      result = root.to_h

      assert_equal true, parser.has_named_captures
      assert_equal 2, parser.num_capture_groups

      assert_equal :concat, result[:type]
      assert_equal 3, result[:children].length

      first = result[:children][0]
      assert_equal :capture, first[:type]
      assert_equal true, first[:has_name]
      assert_equal 'a', first[:name]
      assert_equal 1, first[:capture_num]

      second = result[:children][1]
      assert_equal :group, second[:type]
      assert_equal :literal, second[:child][:type]
      assert_equal 'y', second[:child][:buf]

      third = result[:children][2]
      assert_equal :capture, third[:type]
      assert_equal true, third[:has_name]
      assert_equal 'b', third[:name]
      assert_equal 2, third[:capture_num]
    end

    def test_postprocess_is_noop_when_no_named_capture_exists
      parser = Naraku::Parser.new(Naraku::Encoding::UTF_8, '(a)(b)')
      root = parser.parse
      before = root.to_h

      parser.postprocess(root)
      after = root.to_h

      assert_equal false, parser.has_named_captures
      assert_equal 2, parser.num_capture_groups

      assert_equal before, after
      assert_equal :concat, after[:type]
      assert_equal :capture, after[:children][0][:type]
      assert_equal 1, after[:children][0][:capture_num]
      assert_equal :capture, after[:children][1][:type]
      assert_equal 2, after[:children][1][:capture_num]
    end

    def test_resolved_capture_nums_for_named_back_ref_and_conditional
      parser = Naraku::Parser.new(
        Naraku::Encoding::UTF_8,
        '(?:(?<foo>1)(?<foo>2)\k<foo>(?(<foo>)a|b)(?<foo>3)(?<foo>4))*'
      )
      root = parser.parse
      parser.postprocess(root)
      result = root.to_h

      concat = result[:child][:child]
      assert_equal [1, 2], concat[:children][2][:resolved_capture_nums]
      assert_equal [1, 2], concat[:children][3][:resolved_capture_nums]
    end

    def test_resolved_capture_nums_for_numeric_back_ref_and_conditional_without_named_capture
      parser = Naraku::Parser.new(Naraku::Encoding::UTF_8, '(a)(b)\2(?(1)x|y)')
      root = parser.parse
      parser.postprocess(root)
      result = root.to_h

      assert_equal [2], result[:children][2][:resolved_capture_nums]
      assert_equal [1], result[:children][3][:resolved_capture_nums]
    end

    def test_call_resolved_capture_num
      parser = Naraku::Parser.new(Naraku::Encoding::UTF_8, '(?<a>x)\g<a>\g<0>')
      root = parser.parse
      parser.postprocess(root)
      result = root.to_h

      assert_equal 1, result[:children][1][:resolved_capture_num]
      assert_equal 0, result[:children][2][:resolved_capture_num]
    end

    def test_warn_named_group_defined_after_reference
      warnings = collect_postprocess_warnings('(?:(?<foo>1)(?<foo>2)\k<foo>(?<foo>3)(?<foo>4))*')
      assert_equal 1, warnings.length
      assert_equal 'named group defined after this reference will not be used', warnings[0][:message]
      assert_equal 21, warnings[0][:offset]
      assert_equal 7, warnings[0][:length]
    end

    def test_invalid_reference_kind_mismatch_errors
      assert_postprocess_error('(?<a>x)\1', 'invalid back reference', offset: 7, length: 2)
      assert_postprocess_error('(?<a>x)(?(1)a|b)', 'invalid conditional group', offset: 7, length: 9)
      assert_postprocess_error('(?<a>x)\g<1>', 'invalid sub-expression call', offset: 7, length: 5)

      assert_postprocess_error('(a)\k<a>', 'invalid back reference', offset: 3, length: 5)
      assert_postprocess_error('(a)(?(<a>)x|y)', 'invalid conditional group', offset: 3, length: 11)
      assert_postprocess_error('(a)\g<a>', 'invalid sub-expression call', offset: 3, length: 5)
    end

    def test_undefined_reference_errors
      assert_postprocess_error('\k<a>(?<a>x)', 'undefined back reference', offset: 0, length: 5)
      assert_postprocess_error('(?(<a>)x|y)(?<a>x)', 'undefined conditional reference', offset: 0, length: 11)
      assert_postprocess_error('\g<a>(?<b>x)', 'undefined sub-expression call', offset: 0, length: 5)
      assert_postprocess_error('\g<a>(?<a>x)(?<a>y)', 'invalid sub-expression call', offset: 0, length: 5)
      assert_postprocess_error('\g<a>(a)', 'invalid sub-expression call', offset: 0, length: 5)
      assert_postprocess_error('\g<2>(a)', 'undefined sub-expression call', offset: 0, length: 5)
    end
  end
