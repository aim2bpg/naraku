#!/usr/bin/env ruby
# frozen_string_literal: true

# generate_report.rb - Reads results/*.json and generates a Markdown report.

require 'json'
require 'fileutils'

BENCHMARK_DIR = File.expand_path(__dir__)
RESULTS_DIR   = File.join(BENCHMARK_DIR, 'results')
REPORT_PATH   = File.join(RESULTS_DIR, 'report.md')

SUITE_LABELS = {
  'synthetic' => 'Synthetic Benchmarks',
  'corpus' => 'Corpus-based Benchmarks',
}.freeze

MODES = %w[plain yjit zjit].freeze

def latest_result_for(data, mode, suite, label)
  results = data.dig(mode, suite) || []
  results.rfind { |r| r['label'] == label }
end

def entries_for(data, mode, suite, label)
  latest_result_for(data, mode, suite, label)&.dig('entries') || []
end

def format_ips(val)
  val = val.to_f
  if val >= 1_000_000
    format('%.3f M i/s', val / 1_000_000)
  elsif val >= 1_000
    format('%.3f k i/s', val / 1_000)
  else
    format('%.3f i/s', val)
  end
end

def relative_label(ratio)
  return '(baseline)' if ratio.nil?

  if ratio >= 1.0
    format('**%.2fx faster**', ratio)
  else
    format('**%.2fx slower**', 1.0 / ratio)
  end
end

# -- Data Loading --
# Structure: { mode => { suite => [ { label, entries: [{name, ips, ...}] } ] } }
data = {}
MODES.each do |mode|
  path = File.join(RESULTS_DIR, "#{mode}.json")
  next unless File.exist?(path)

  raw = JSON.parse(File.read(path))
  raw.each do |block|
    suite   = block['suite']
    results = block['results']
    data[mode] ||= {}
    data[mode][suite] ||= []
    data[mode][suite].concat(results)
  end
end

if data.empty?
  warn "No result JSON files found in #{RESULTS_DIR}. Run benchmarks first."
  exit 1
end

# -- Report Generation --
lines = []
lines << '# NarakuRuby::DFA Benchmark Report'
lines << ''
lines << "> Generated at #{Time.now.strftime('%Y-%m-%d %H:%M:%S %z')}"
lines << ''
lines << "Ruby version: `#{RUBY_DESCRIPTION}`"
lines << ''

present_modes = MODES.select { |m| data.key?(m) }
lines << '## Environment'
lines << ''
lines << '| Mode | Description |'
lines << '|---|---|'
lines << '| `plain` | No JIT |' if present_modes.include?('plain')
lines << '| `yjit`  | YJIT enabled (`--yjit`) |' if present_modes.include?('yjit')
lines << '| `zjit`  | ZJIT enabled (`--zjit`) |' if present_modes.include?('zjit')
lines << ''

# -- Sections per suite --
all_suites = data.values.flat_map(&:keys).uniq.sort_by { |s| SUITE_LABELS.keys.index(s) || 99 }

all_suites.each do |suite|
  suite_label = SUITE_LABELS.fetch(suite, suite)
  lines << '---'
  lines << ''
  lines << "## #{suite_label}"
  lines << ''

  all_labels = present_modes.flat_map { |m| (data.dig(m, suite) || []).map { |r| r['label'] } }.uniq

  all_labels.each do |label|
    lines << "### #{label}"
    lines << ''

    pattern = present_modes.filter_map { |m| latest_result_for(data, m, suite, label)&.dig('pattern') }.first
    description = present_modes.filter_map { |m| latest_result_for(data, m, suite, label)&.dig('description') }.first

    lines << "> **Pattern**: `#{pattern}`" if pattern
    lines << ">\n> #{description}" if description
    lines << ''

    lines << '| Implementation (Mode) | IPS | Ratio to DFA(plain) |'
    lines << '|---|---:|---|'

    all_names = present_modes.flat_map do |m|
      entries_for(data, m, suite, label).map { |e| e['name'] }
    end.uniq

    # Find the DFA baseline in plain mode for this specific test case.
    # The name usually includes 'NarakuRuby::DFA' or 'NarakuRuby.parse'.
    dfa_name = all_names.find { |n| n == 'NarakuRuby::DFA#match?' } ||
               all_names.find { |n| n.include?('NarakuRuby') }
    dfa_plain_ips = nil
    if dfa_name
      entries_plain = entries_for(data, 'plain', suite, label)
      dfa_plain_ips = entries_plain.find { |e| e['name'] == dfa_name }&.dig('ips')
    end

    all_names.each do |name|
      present_modes.each do |mode|
        entries = entries_for(data, mode, suite, label)
        entry   = entries.find { |e| e['name'] == name }
        next unless entry # skip if this name/mode combo doesn't exist

        ips = entry['ips']

        # Determine if this row IS the baseline
        is_baseline_row = (name == dfa_name) && (mode == 'plain')

        ratio = dfa_plain_ips&.positive? ? ips.to_f / dfa_plain_ips : nil
        ratio_text = is_baseline_row ? '---' : relative_label(ratio)

        lines << "| `#{name}` (`#{mode}`) | #{format_ips(ips)} | #{ratio_text} |"
      end
    end

    lines << ''
  end
end

FileUtils.mkdir_p(RESULTS_DIR)
File.write(REPORT_PATH, lines.join("\n"))
puts "Report written to #{REPORT_PATH}"
