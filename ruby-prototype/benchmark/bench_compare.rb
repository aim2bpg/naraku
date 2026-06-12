# frozen_string_literal: true

# Comparison benchmark: Onigmo (CRuby) vs NarakuRuby::DFA (CRuby) vs Naraku Pike VM (mruby)
#
# Usage:
#   bundle exec ruby ruby-prototype/benchmark/bench_compare.rb
#   bundle exec ruby ruby-prototype/benchmark/bench_compare.rb --yjit  (Ruby 3.1+)

$LOAD_PATH.unshift File.expand_path('../lib', __dir__)
require 'naraku_ruby'

PROJECT_ROOT = File.expand_path('../..', __dir__)
MRUBY_BIN    = File.join(PROJECT_ROOT, 'bin/mruby')
PIKE_SCRIPT  = File.join(PROJECT_ROOT, 'tools/bench_pike_vm.rb')

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
    label: 'pathological: (?:a?){30}a{30}',
    pattern: '(?:a?){30}a{30}',
    inputs: (['a' * 30] * 1000) + (["#{'a' * 30}b"] * 500),
  },
  {
    label: 'unicode: ジョバンニ|カムパネルラ',
    pattern: 'ジョバンニ|カムパネルラ',
    inputs: (['ジョバンニ'] * 700) + (['カムパネルラ'] * 700) + (['銀河鉄道'] * 400),
  },
].freeze

# Simple timing loop: returns iterations-per-second
def time_bench(inputs, &)
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  inputs.each(&) while Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0 < WARMUP_SEC

  count = 0
  t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  while Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0 < MEASURE_SEC
    inputs.each(&)
    count += 1
  end
  elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0
  count.to_f / elapsed
end

def format_ips(ips)
  return '    n/a   ' if ips.nil?

  format('%9.1f', ips)
end

def ratio_label(vm_ips, baseline_ips)
  return '  n/a' if vm_ips.nil? || baseline_ips.nil? || baseline_ips.zero?

  r = vm_ips / baseline_ips
  format('%5.2fx', r)
end

# ── CRuby measurements ───────────────────────────────────────────────────────

jit_mode =
  if defined?(RubyVM::ZJIT) && RubyVM::ZJIT.enabled?
    'ZJIT'
  elsif defined?(RubyVM::YJIT) && RubyVM::YJIT.enabled?
    'YJIT'
  else
    'plain'
  end

puts "Measuring Onigmo + DFA on CRuby (#{jit_mode})..."

crb_results = {}
CASES.each do |c|
  print "  #{c[:label]}... "
  $stdout.flush

  re = Regexp.new(c[:pattern])
  onigmo_ips = time_bench(c[:inputs]) { |s| re.match?(s) }

  dfa_ips = begin
    dfa = NarakuRuby::DFA.compile(c[:pattern])
    time_bench(c[:inputs]) { |s| dfa.match?(s) }
  rescue StandardError
    nil
  end

  crb_results[c[:label]] = { onigmo: onigmo_ips, dfa: dfa_ips }
  puts 'done'
end

# ── mruby Pike VM measurement ────────────────────────────────────────────────

puts "\nMeasuring Pike VM on mruby..."
pike_results = {}

IO.popen([MRUBY_BIN, PIKE_SCRIPT], 'r') do |io|
  io.each_line do |line|
    type, label, value = line.chomp.split("\t", 3)
    case type
    when 'RESULT'
      pike_results[label] = value.to_f
      puts "  #{label}: #{value} ips"
    when 'SKIP'
      pike_results[label] = nil
      puts "  [SKIP] #{label}: #{value}"
    else
      puts "  [?] #{line.chomp}"
    end
  end
end

# ── Results table ────────────────────────────────────────────────────────────

col_w   = 34
num_w   = 10
ratio_w = 7

hr = '-' * (col_w + (num_w * 3) + (ratio_w * 2) + 10)

puts
puts hr
puts format("%-#{col_w}s  %#{num_w}s  %#{num_w}s  %#{num_w}s  %#{ratio_w}s  %#{ratio_w}s",
            'Pattern', 'Onigmo(C)', 'LazyDFA(Rb)', 'PikeVM(mrb)', 'DFA/Ong', 'VM/Ong')
puts hr

CASES.each do |c|
  lbl    = c[:label]
  cr     = crb_results[lbl] || {}
  ong    = cr[:onigmo]
  dfa    = cr[:dfa]
  pike   = pike_results[lbl]

  puts format("%-#{col_w}s  %s  %s  %s  %s  %s",
              lbl[0, col_w],
              format_ips(ong),
              format_ips(dfa),
              format_ips(pike),
              ratio_label(dfa, ong),
              ratio_label(pike, ong))
end

puts hr
puts
puts 'ips = full input-batch iterations per second (higher is better)'
puts "Onigmo/LazyDFA: CRuby #{RUBY_VERSION} (#{jit_mode})"
puts 'PikeVM:     mruby (no JIT)'
puts 'Note: runtimes differ — ratio is approximate'
