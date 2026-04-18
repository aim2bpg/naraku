#!/usr/bin/env ruby
# frozen_string_literal: true

# run_all.rb - Executes benchmarks across different JIT modes and saves JSON results.
#
# Usage:
#   bundle exec ruby ruby-prototype/benchmark/run_all.rb [--suites=synthetic,corpus,compile]
#
# Options:
#   --suites=LIST  Comma-separated list of suites (default: all)
#   --modes=LIST   Comma-separated list of JIT modes (default: plain,yjit,zjit)
#   --report       Generate report after benchmarking

require 'fileutils'
require 'open3'
require 'json'
require 'English'

BENCHMARK_DIR = File.expand_path(__dir__)
RESULTS_DIR   = File.join(BENCHMARK_DIR, 'results')
RUBY          = RbConfig.ruby

ALL_SUITES = %w[synthetic corpus].freeze
ALL_MODES  = %w[plain yjit zjit].freeze

# -- CLI Argument Parsing --
suites  = ALL_SUITES
modes   = ALL_MODES
do_report = false

ARGV.each do |arg|
  case arg
  when /\A--suites=(.+)\z/
    suites = Regexp.last_match(1).split(',').map(&:strip) & ALL_SUITES
  when /\A--modes=(.+)\z/
    modes = Regexp.last_match(1).split(',').map(&:strip) & ALL_MODES
  when '--report'
    do_report = true
  end
end

# -- Preparation --
FileUtils.mkdir_p(RESULTS_DIR)

# Remove existing JSON results for a fresh run
modes.each { |m| FileUtils.rm_f(File.join(RESULTS_DIR, "#{m}.json")) }

# -- Mode Flags --
MODE_FLAGS = {
  'plain' => [],
  'yjit' => ['--yjit'],
  'zjit' => ['--zjit'],
}.freeze

# -- Benchmark Execution --
modes.each do |mode|
  flags = MODE_FLAGS[mode]

  suites.each do |suite|
    bench_file = File.join(BENCHMARK_DIR, "bench_#{suite}.rb")
    unless File.exist?(bench_file)
      warn "  [SKIP] bench file not found: #{bench_file}"
      next
    end

    cmd = [RUBY, *flags, bench_file]
    puts "\n#{'=' * 70}"
    puts "  Running: #{cmd.join(' ')}"
    puts '=' * 70

    # Stream output from the child process
    IO.popen(cmd, err: %i[child out]) do |io|
      io.each_line { |line| print line }
    end

    status = $CHILD_STATUS
    warn "\n  [ERROR] Benchmark failed (exit #{status.exitstatus})" unless status.success?
  end
end

puts "\n#{'-' * 70}"
puts "  All benchmarks done. Results in #{RESULTS_DIR}"
puts '-' * 70

# -- Report Generation --
if do_report
  report_script = File.join(BENCHMARK_DIR, 'generate_report.rb')
  if File.exist?(report_script)
    puts "\nGenerating report..."
    system(RUBY, report_script)
  else
    warn '  [SKIP] generate_report.rb not found'
  end
end
