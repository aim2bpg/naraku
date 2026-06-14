class ErrorMessageTest < Mtest::Test
  def test_error_message_returns_nil_for_success
    assert_nil Naraku.error_message(0)
  end

  def test_error_message_for_runtime_codes
    assert_equal 'no match', Naraku.error_message(-1)
    assert_equal 'memory allocation failed', Naraku.error_message(-100)
  end

  def test_error_message_for_internal_bug_codes
    assert_equal 'BUG: internal error', Naraku.error_message(-200)
    assert_equal 'BUG: parser', Naraku.error_message(-201)
  end

  def test_error_message_for_uncovered_parser_codes
    assert_equal 'unexpected end of pattern', Naraku.error_message(-302)
    assert_equal 'invalid escape sequence', Naraku.error_message(-311)
  end

  def test_error_message_for_compiler_unsupported_codes
    assert_equal 'unsupported feature in this version', Naraku.error_message(-600)
    assert_equal 'ignore-case matching is not supported in this version', Naraku.error_message(-601)
    assert_equal 'back references are not supported in this version', Naraku.error_message(-602)
    assert_equal 'sub-expression calls are not supported in this version', Naraku.error_message(-603)
    assert_equal 'lookaround assertions are not supported in this version', Naraku.error_message(-604)
    assert_equal 'atomic groups are not supported in this version', Naraku.error_message(-605)
    assert_equal 'absence groups are not supported in this version', Naraku.error_message(-606)
    assert_equal 'conditional groups are not supported in this version', Naraku.error_message(-607)
    assert_equal 'possessive quantifiers are not supported in this version', Naraku.error_message(-608)
    assert_equal 'pattern is too complex to compile', Naraku.error_message(-610)
  end

  def test_error_message_returns_bug_unknown_for_unknown_code
    assert_equal 'BUG: unknown error', Naraku.error_message(9999)
  end

  def test_warning_message_returns_bug_unknown_for_unknown_code
    assert_equal 'BUG: unknown warning', Naraku.warning_message(9999)
  end
end
