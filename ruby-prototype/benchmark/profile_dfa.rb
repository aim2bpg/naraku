# frozen_string_literal: true

require_relative 'bench_helper'
require 'fileutils'
require 'open3'

begin
  require 'stackprof'
rescue LoadError
  warn '[WARN] stackprof is not installed. Run `bundle install` to enable CPU/wall profiling.'
end

begin
  require 'memory_profiler'
rescue LoadError
  warn '[WARN] memory_profiler is not installed. Run `bundle install` to enable allocation profiling.'
end

RESULTS_PROFILE_DIR = File.join(RESULTS_DIR, 'profiles')
FileUtils.mkdir_p(RESULTS_PROFILE_DIR)

PROFILE_CASES = {
  'repetition-a-plus-b' => {
    pattern: 'a+b',
    inputs: (["#{'a' * 100}b"] * 300) + (['x' * 100] * 150),
  },
  'pathological-aq30-a30' => {
    pattern: '(?:a?){30}a{30}',
    inputs: (['a' * 30] * 120) + (["#{'a' * 30}b"] * 60),
  },
  'word-boundary-holmes' => {
    pattern: '\bHolmes\b',
    inputs: CORPUS_ADVS_LINES.first(600),
  },
  'katakana-class' => {
    pattern: '[ァ-ヶー]+',
    inputs: CORPUS_GINGA_LINES.first(600),
  },
}.freeze

def parse_options
  opts = {
    kase: nil,
    iterations: 20,
    stackprof_modes: %i[cpu wall],
    profile_memory: true,
  }

  ARGV.each do |arg|
    case arg
    when /\A--case=(.+)\z/
      opts[:kase] = Regexp.last_match(1)
    when /\A--iterations=(\d+)\z/
      opts[:iterations] = Regexp.last_match(1).to_i
    when /\A--stackprof=(.+)\z/
      opts[:stackprof_modes] = Regexp.last_match(1).split(',').map(&:strip).map(&:to_sym)
    when '--no-memory'
      opts[:profile_memory] = false
    end
  end
  opts
end

def run_workload(dfa, inputs, iterations)
  iterations.times { inputs.each { |s| dfa.match?(s) } }
end

def write_stackprof(case_name, mode, dfa, inputs, iterations)
  dump_path = File.join(RESULTS_PROFILE_DIR, "#{case_name}.#{mode}.stackprof.dump")
  text_path = File.join(RESULTS_PROFILE_DIR, "#{case_name}.#{mode}.stackprof.txt")

  StackProf.run(mode:, out: dump_path) { run_workload(dfa, inputs, iterations) }
  output, status = Open3.capture2(Gem.ruby, '-S', 'stackprof', dump_path, '--text', '--limit', '200')
  if status.success?
    File.write(text_path, output)
    puts "  stackprof(#{mode}) -> #{text_path}"
  else
    warn "  [WARN] failed to render stackprof text report for #{dump_path}"
  end
end

def write_memory_profile(case_name, dfa, inputs, iterations)
  path = File.join(RESULTS_PROFILE_DIR, "#{case_name}.memory.txt")
  report = MemoryProfiler.report { run_workload(dfa, inputs, iterations) }
  report.pretty_print(to_file: path, detailed_report: true, scale_bytes: true, normalize_paths: true)
  puts "  memory_profiler -> #{path}"
end

opts = parse_options
selected_cases = if opts[:kase]
                   PROFILE_CASES.slice(opts[:kase])
                 else
                   PROFILE_CASES
                 end

if selected_cases.empty?
  warn "No matching profile case. Available: #{PROFILE_CASES.keys.join(', ')}"
  exit 1
end

puts "Profiling mode=#{BenchHelper.jit_mode} iterations=#{opts[:iterations]}"
selected_cases.each do |case_name, cfg|
  puts "\n--- #{case_name} ---"
  puts "  pattern: #{cfg[:pattern].inspect}"
  puts "  inputs: #{cfg[:inputs].length}"

  dfa = BenchHelper.compile_dfa(cfg[:pattern])
  next unless dfa

  opts[:stackprof_modes].each do |mode|
    next unless defined?(StackProf)

    write_stackprof(case_name, mode, dfa, cfg[:inputs], opts[:iterations])
  end

  write_memory_profile(case_name, dfa, cfg[:inputs], opts[:iterations]) if opts[:profile_memory] && defined?(MemoryProfiler)
end
