# frozen_string_literal: true

require 'bundler/setup'
require 'benchmark'
require 'benchmark/ips'
require 'fileutils'
require 'json'

BENCHMARK_DIR = File.expand_path(__dir__)
DATA_DIR    = File.join(BENCHMARK_DIR, 'data')
RESULTS_DIR = File.join(BENCHMARK_DIR, 'results')

$LOAD_PATH.unshift File.join(BENCHMARK_DIR, '../lib')
require 'naraku_ruby'

# -- Corpus Data --
CORPUS_ADVS        = File.read(File.join(DATA_DIR, 'advs.txt'), encoding: 'UTF-8').freeze
CORPUS_GINGA       = File.read(File.join(DATA_DIR, 'gingatetsudono_yoru.txt'), encoding: 'UTF-8').freeze
CORPUS_ADVS_LINES  = CORPUS_ADVS.lines.freeze
CORPUS_GINGA_LINES = CORPUS_GINGA.lines.freeze

# -- Utilities --
module BenchHelper
  WARMUP_SEC = 2
  TIME_SEC   = 5

  # Compiles DFA program. Returns nil if compilation fails.
  def self.compile_dfa(pattern)
    NarakuRuby::DFA.compile(pattern)
  rescue StandardError => e
    warn "  [SKIP] DFA compile error for #{pattern.inspect}: #{e.message}"
    nil
  end

  # Returns the JIT mode label
  def self.jit_mode
    if defined?(RubyVM::ZJIT) && RubyVM::ZJIT.enabled?
      'zjit'
    elsif defined?(RubyVM::YJIT) && RubyVM::YJIT.enabled?
      'yjit'
    else
      'plain'
    end
  end

  # Appends the suite results to results/<mode>.json
  # accumulated is an array of { label:, pattern:, entries: [...] }
  def self.write_results(suite_name, accumulated)
    FileUtils.mkdir_p(RESULTS_DIR)
    mode     = jit_mode
    out_path = File.join(RESULTS_DIR, "#{mode}.json")

    all = File.exist?(out_path) ? JSON.parse(File.read(out_path)) : []
    all << { 'suite' => suite_name, 'mode' => mode, 'results' => accumulated }
    File.write(out_path, JSON.pretty_generate(all))
    puts "  => Results appended to #{out_path}"
  end
end
