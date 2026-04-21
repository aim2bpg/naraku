# frozen_string_literal: true

require_relative 'bench_helper'

# -- Synthetic Inputs Benchmark --
#
# Compares execution speed for each construct using short patterns and fixed synthetic strings.

SYNTHETIC_CASES = [
  {
    label: 'literal: simple word',
    pattern: 'Watson',
    inputs: (['Watson'] * 1000) + (['Moriarty'] * 1000),
    full_dfa_compare: true,
  },
  {
    label: 'alternation: 3 branches',
    pattern: 'foo|bar|baz',
    inputs: (['foo'] * 500) + (['bar'] * 500) + (['qux'] * 1000),
    full_dfa_compare: true,
  },
  {
    label: 'repetition: greedy a+b',
    pattern: 'a+b',
    inputs: (["#{'a' * 100}b"] * 1000) + (['x' * 100] * 500),
    full_dfa_compare: true,
  },
  {
    label: 'repetition: ambiguous (a|a)+b',
    pattern: '(a|a)+b',
    inputs: (["#{'a' * 100}b"] * 1000) + (['x' * 100] * 500),
    full_dfa_compare: true,
  },
  {
    label: 'char_class: alphanumeric',
    pattern: '[a-zA-Z0-9]+',
    inputs: (['abc123XYZ'] * 1000) + (['!!!'] * 500),
  },
  {
    label: 'anchor: begin-of-line',
    pattern: '^Hello',
    inputs: (["Hello world\nGoodbye"] * 500) + (["Goodbye\nHello world"] * 500),
  },
  {
    label: 'bounded quantifier: date',
    pattern: '\d{4}-\d{2}-\d{2}',
    inputs: (['2024-01-15'] * 1000) + (['not-a-date'] * 500),
  },
  {
    label: 'pathological: catastrophic backtracking (?:a?){n}a{n}',
    pattern: '(?:a?){30}a{30}',
    inputs: (['a' * 30] * 1000) + (["#{'a' * 30}b"] * 500),
  },
].freeze

SYNTHETIC_WITH_CAPS_CASES = [
  {
    label: 'with_caps: email-like local/domain',
    pattern: '(\w+)@(\w+)',
    inputs: (['user@example'] * 1000) + (['invalid'] * 500),
  },
  {
    label: 'with_caps: optional protocol + host/path',
    pattern: '(https?)://([^/]+)(/.*)?',
    inputs: (['https://example.com/path/to/page'] * 800) + (['http://localhost'] * 400) + (['not a url'] * 500),
  },
  {
    label: 'with_caps: date capture groups',
    pattern: '(\d{4})-(\d{2})-(\d{2})',
    inputs: (['2024-01-15'] * 1000) + (['2024-1-5'] * 250) + (['not-a-date'] * 250),
  },
  {
    label: 'with_caps: word boundary capture',
    pattern: '\b([A-Za-z]+)\b',
    inputs: (['Holmes'] * 700) + (['Dr.Watson'] * 700) + (['12345'] * 400),
  },
  {
    label: 'with_caps: unicode alternation + honorific',
    pattern: '(ジョバンニ|カムパネルラ)(君|さん)?',
    inputs: (['ジョバンニ君'] * 700) + (['カムパネルラさん'] * 700) + (['銀河鉄道'] * 400),
  },
].freeze

def run_synthetic_benchmarks
  mode = BenchHelper.jit_mode
  puts "\n#{'=' * 60}"
  puts "  Synthetic Benchmarks  [#{mode}]"
  puts '=' * 60

  accumulated = []

  SYNTHETIC_CASES.each do |bcase|
    pattern = bcase[:pattern]
    inputs  = bcase[:inputs]

    dfa = BenchHelper.compile_dfa(pattern)
    full_dfa = bcase[:full_dfa_compare] ? BenchHelper.compile_dfa(pattern, full_dfa: true) : nil
    full_dfa_eval = bcase[:full_dfa_compare] ? BenchHelper.compile_dfa(pattern, full_dfa: true, eval: true) : nil
    re = Regexp.new(pattern)

    puts "\n--- #{bcase[:label]} ---"
    puts "    pattern: #{pattern.inspect}  inputs: #{inputs.length} strings"

    job = Benchmark.ips do |x|
      x.config(warmup: BenchHelper::WARMUP_SEC, time: BenchHelper::TIME_SEC)

      x.report('NarakuRuby::DFA#match?') { inputs.each { |s| dfa.match?(s) } } if dfa
      x.report('NarakuRuby::DFA#match? (full_dfa)') { inputs.each { |s| full_dfa.match?(s) } } if full_dfa
      x.report('NarakuRuby::DFA#match? (full_dfa+eval)') { inputs.each { |s| full_dfa_eval.match?(s) } } if full_dfa_eval
      x.report('Regexp#match?') { inputs.each { |s| re.match?(s) } }

      x.compare!
    end

    entries = job.entries.map do |e|
      {
        'name' => e.label,
        'ips' => e.ips,
        'stddev' => e.stats.error,
        'microseconds' => e.microseconds,
        'iterations' => e.iterations,
      }
    end
    accumulated << { 'label' => bcase[:label], 'pattern' => pattern, 'entries' => entries }
  end

  SYNTHETIC_WITH_CAPS_CASES.each do |bcase|
    pattern = bcase[:pattern]
    inputs  = bcase[:inputs]

    dfa = BenchHelper.compile_dfa(pattern)
    re = Regexp.new(pattern)

    puts "\n--- #{bcase[:label]} ---"
    puts "    pattern: #{pattern.inspect}  inputs: #{inputs.length} strings"

    job = Benchmark.ips do |x|
      x.config(warmup: BenchHelper::WARMUP_SEC, time: BenchHelper::TIME_SEC)

      x.report('NarakuRuby::DFA#match') { inputs.each { |s| dfa.match(s) } } if dfa
      x.report('Regexp#match') { inputs.each { |s| re.match(s) } }

      x.compare!
    end

    entries = job.entries.map do |e|
      {
        'name' => e.label,
        'ips' => e.ips,
        'stddev' => e.stats.error,
        'microseconds' => e.microseconds,
        'iterations' => e.iterations,
      }
    end
    accumulated << { 'label' => bcase[:label], 'pattern' => pattern, 'entries' => entries }
  end

  BenchHelper.write_results('synthetic', accumulated)
end

run_synthetic_benchmarks
