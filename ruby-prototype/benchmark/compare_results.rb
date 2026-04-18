# frozen_string_literal: true

require 'json'

def parse_options(argv)
  opts = {
    impl: 'NarakuRuby::DFA#match?',
    out: nil,
    name_before: 'before',
    name_after: 'after',
    min_ratio: nil,
  }
  paths = []

  argv.each do |arg|
    case arg
    when /\A--impl=(.+)\z/
      opts[:impl] = Regexp.last_match(1)
    when /\A--out=(.+)\z/
      opts[:out] = Regexp.last_match(1)
    when /\A--name-before=(.+)\z/
      opts[:name_before] = Regexp.last_match(1)
    when /\A--name-after=(.+)\z/
      opts[:name_after] = Regexp.last_match(1)
    when /\A--min-ratio=(.+)\z/
      opts[:min_ratio] = Regexp.last_match(1).to_f
    else
      paths << arg
    end
  end

  return opts, paths if paths.length == 2

  warn "Usage: #{File.basename($PROGRAM_NAME)} [--impl=LABEL] [--out=PATH] [--name-before=NAME] [--name-after=NAME] [--min-ratio=FLOAT] BEFORE.json AFTER.json"
  exit 1
end

def load_impl_scores(path, impl)
  raw = JSON.parse(File.read(path))
  scores = {}
  raw.each do |suite_block|
    suite = suite_block.fetch('suite')
    suite_block.fetch('results').each do |result|
      entry = result.fetch('entries').find { |e| e['name'] == impl }
      next unless entry

      key = [suite, result.fetch('label')]
      scores[key] = {
        suite:,
        label: result.fetch('label'),
        pattern: result['pattern'],
        ips: entry.fetch('ips').to_f,
      }
    end
  end
  scores
end

def format_ips(ips)
  return format('%.3f M i/s', ips / 1_000_000) if ips >= 1_000_000
  return format('%.3f k i/s', ips / 1_000) if ips >= 1_000

  format('%.3f i/s', ips)
end

opts, paths = parse_options(ARGV)
before_path, after_path = paths

before_scores = load_impl_scores(before_path, opts[:impl])
after_scores = load_impl_scores(after_path, opts[:impl])
keys = (before_scores.keys & after_scores.keys).sort

if keys.empty?
  warn "No common rows found for implementation #{opts[:impl].inspect}."
  exit 1
end

lines = []
lines << "# Benchmark Comparison (#{opts[:impl]})"
lines << ''
lines << "| Suite | Case | #{opts[:name_before]} | #{opts[:name_after]} | Ratio (after/before) |"
lines << '|---|---|---:|---:|---:|'

keys.each do |key|
  before = before_scores.fetch(key)
  after = after_scores.fetch(key)
  ratio = after[:ips] / before[:ips]
  lines << "| #{before[:suite]} | #{before[:label]} | #{format_ips(before[:ips])} | #{format_ips(after[:ips])} | #{format('%.2fx', ratio)} |"
end

report = "#{lines.join("\n")}\n"
if opts[:out]
  File.write(opts[:out], report)
  puts "Comparison report written to #{opts[:out]}"
else
  puts report
end

if opts[:min_ratio]
  below_threshold = keys.filter_map do |key|
    before = before_scores.fetch(key)
    after = after_scores.fetch(key)
    ratio = after[:ips] / before[:ips]
    "#{before[:suite]} / #{before[:label]} (#{format('%.2fx', ratio)})" if ratio < opts[:min_ratio]
  end

  unless below_threshold.empty?
    warn "Performance regression detected (< #{opts[:min_ratio]}x):"
    below_threshold.each { |line| warn "  - #{line}" }
    exit 1
  end
end
