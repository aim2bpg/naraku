# frozen_string_literal: true

# Comparison benchmark: Onigmo (CRuby) vs NarakuRuby::DFA (CRuby) vs
# Naraku::Regexp (CRuby native ext, ext/naraku/) vs Naraku Pike VM (mruby)
#
# The CRuby native extension runs the exact same C core (src/regex_compile.c,
# src/regex_vm.c, ...) as the mruby Pike VM column, but under CRuby (+YJIT),
# removing the mruby-vs-CRuby runtime handicap from the comparison. Build it
# first with `bundle exec rake naraku:build_cruby_ext`; if missing, that
# column reports n/a rather than failing the whole benchmark.
#
# Usage:
#   bundle exec ruby ruby-prototype/benchmark/bench_compare.rb
#   bundle exec ruby ruby-prototype/benchmark/bench_compare.rb --yjit  (Ruby 3.1+)

$LOAD_PATH.unshift File.expand_path('../lib', __dir__)
require 'naraku_ruby'

PROJECT_ROOT = File.expand_path('../..', __dir__)
MRUBY_BIN    = File.join(PROJECT_ROOT, 'bin/mruby')
PIKE_SCRIPT  = File.join(PROJECT_ROOT, 'tools/bench_pike_vm.rb')
CEXT_DIR     = File.join(PROJECT_ROOT, 'ext/naraku')

naraku_cext_available =
  begin
    $LOAD_PATH.unshift CEXT_DIR
    require 'naraku_cext'
    true
  rescue LoadError
    false
  end

require_relative 'bench_cases'

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

  cext_ips =
    if naraku_cext_available
      begin
        cext_re = Naraku::Regexp.new(c[:pattern])
        time_bench(c[:inputs]) { |s| cext_re.match?(s) }
      rescue StandardError
        nil
      end
    end

  crb_results[c[:label]] = { onigmo: onigmo_ips, dfa: dfa_ips, cext: cext_ips }
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

hr = '-' * (col_w + (num_w * 4) + (ratio_w * 3) + 12)

puts
puts hr
puts format("%-#{col_w}s  %#{num_w}s  %#{num_w}s  %#{num_w}s  %#{num_w}s  %#{ratio_w}s  %#{ratio_w}s  %#{ratio_w}s",
            'Pattern', 'Onigmo(C)', 'LazyDFA(Rb)', 'NarakuCExt(C)', 'PikeVM(mrb)', 'DFA/Ong', 'CExt/Ong', 'VM/Ong')
puts hr

CASES.each do |c|
  lbl    = c[:label]
  cr     = crb_results[lbl] || {}
  ong    = cr[:onigmo]
  dfa    = cr[:dfa]
  cext   = cr[:cext]
  pike   = pike_results[lbl]

  puts format("%-#{col_w}s  %s  %s  %s  %s  %s  %s  %s",
              lbl[0, col_w],
              format_ips(ong),
              format_ips(dfa),
              format_ips(cext),
              format_ips(pike),
              ratio_label(dfa, ong),
              ratio_label(cext, ong),
              ratio_label(pike, ong))
end

puts hr
puts
puts 'ips = full input-batch iterations per second (higher is better)'
puts "Onigmo/LazyDFA: CRuby #{RUBY_VERSION} (#{jit_mode})"
puts 'PikeVM:     mruby (no JIT)'
puts 'Note: runtimes differ — ratio is approximate'
