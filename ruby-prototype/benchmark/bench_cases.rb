# frozen_string_literal: true

# Shared benchmark cases for ruby-prototype/benchmark/bench_compare.rb (CRuby)
# and tools/bench_pike_vm.rb (mruby). Kept in one place so a new case only
# needs to be added once instead of two files staying in sync by hand.

WARMUP_SEC  = 1.0
MEASURE_SEC = 3.0

CASES = [
  {
    label: 'literal: Watson',
    pattern: 'Watson',
    inputs: (['Watson'] * 1000) + (['Moriarty'] * 1000),
  },
  {
    label: 'alternation: foo|bar|baz',
    pattern: 'foo|bar|baz',
    inputs: (['foo'] * 500) + (['bar'] * 500) + (['qux'] * 1000),
  },
  {
    label: 'repetition: a+b',
    pattern: 'a+b',
    inputs: (["#{'a' * 100}b"] * 1000) + (['x' * 100] * 500),
  },
  {
    label: 'ambiguous: (a|a)+b',
    pattern: '(a|a)+b',
    inputs: (["#{'a' * 100}b"] * 1000) + (['x' * 100] * 500),
  },
  {
    label: 'char_class: [a-zA-Z0-9]+',
    pattern: '[a-zA-Z0-9]+',
    inputs: (['abc123XYZ'] * 1000) + (['!!!'] * 500),
  },
  {
    label: 'bounded: \\d{4}-\\d{2}-\\d{2}',
    pattern: '\d{4}-\d{2}-\d{2}',
    inputs: (['2024-01-15'] * 1000) + (['not-a-date'] * 500),
  },
  {
    label: 'bounded_non_match: \\d{4}-\\d{2}-\\d{2}',
    pattern: '\d{4}-\d{2}-\d{2}',
    inputs: ['not-a-date'] * 1500,
  },
  {
    label: 'pathological: (?:a?){30}a{30}',
    pattern: '(?:a?){30}a{30}',
    inputs: (['a' * 30] * 1000) + (["#{'a' * 30}b"] * 500),
  },
  {
    label: 'unicode: ジョバンニ|カムパネルラ',
    pattern: 'ジョバンニ|カムパネルラ',
    inputs: (['ジョバンニ'] * 700) + (['カムパネルラ'] * 700) + (['銀河鉄道'] * 400),
  },
  {
    # Long-input variant of char_class: with short (~10 byte) inputs, per-call
    # Ruby-level overhead dominates the measured time and masks engine-level
    # differences (see docs/ja/naraku_vm.md §6.3's batches/sec caveat). A
    # long subject amortises that overhead so the SIMD ASCII run scan
    # (Cycle O) shows up in the batches/sec number instead of being swamped
    # by call dispatch noise.
    label: 'char_class_long: [a-zA-Z0-9]+ (6000 bytes)',
    pattern: '[a-zA-Z0-9]+',
    inputs: ["#{'aB3' * 2000}!", ('!' * 6000)] * 50,
  },
].freeze
