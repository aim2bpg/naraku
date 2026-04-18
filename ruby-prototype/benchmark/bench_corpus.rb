# frozen_string_literal: true

require_relative 'bench_helper'

# -- Corpus-based Realistic Scenario Benchmark --
#
# Applies each pattern to all lines in real text to measure realistic throughput.
# To match Regexp#scan semantics with NarakuRuby::DFA's current API, we iterate over lines and call match?.

CORPUS_CASES = [
  # -- advs.txt (English text) --
  {
    label: 'corpus/advs: ASCII word tokens [A-Za-z]+',
    pattern: '[A-Za-z]+',
    lines: CORPUS_ADVS_LINES,
    description: 'Scanning lines containing English words',
  },
  {
    label: 'corpus/advs: quoted speech "…"',
    pattern: '"[^"]+"',
    lines: CORPUS_ADVS_LINES,
    description: 'Detecting lines with quoted speech',
  },
  {
    label: 'corpus/advs: capitalized words [A-Z][a-z]+',
    pattern: '[A-Z][a-z]+',
    lines: CORPUS_ADVS_LINES,
    description: 'Detecting lines with capitalized words',
  },
  {
    label: 'corpus/advs: word boundary \bHolmes\b',
    pattern: '\bHolmes\b',
    lines: CORPUS_ADVS_LINES,
    description: 'Detecting lines containing the word Holmes (word boundary)',
  },
  {
    label: 'corpus/advs: chapter headings [IVX]+\.',
    pattern: '[IVX]+\.',
    lines: CORPUS_ADVS_LINES,
    description: 'Detecting lines with Roman numeral chapter headings',
  },

  # -- gingatetsudono_yoru.txt (Japanese text) --
  {
    label: 'corpus/ginga: Japanese sentence end [^。、]+[。、]',
    pattern: '[^。、]+[。、]',
    lines: CORPUS_GINGA_LINES,
    description: 'Detecting lines with Japanese clauses ending in punctuation',
  },
  {
    label: 'corpus/ginga: Katakana words [ァ-ヶー]+',
    pattern: '[ァ-ヶー]+',
    lines: CORPUS_GINGA_LINES,
    description: 'Detecting lines containing Katakana words (Unicode range class)',
  },
  {
    label: 'corpus/ginga: character names',
    pattern: 'ジョバンニ|カムパネルラ',
    lines: CORPUS_GINGA_LINES,
    description: 'Detecting lines containing main character names (multi-byte literal alternation)',
  },
  {
    label: 'corpus/ginga: parenthetical （[^）]*）',
    pattern: '（[^）]*）',
    lines: CORPUS_GINGA_LINES,
    description: 'Detecting lines with full-width parenthetical text',
  },
].freeze

def run_corpus_benchmarks
  mode = BenchHelper.jit_mode
  puts "\n#{'=' * 60}"
  puts "  Corpus Benchmarks  [#{mode}]"
  puts '=' * 60

  accumulated = []

  CORPUS_CASES.each do |bcase|
    pattern = bcase[:pattern]
    lines   = bcase[:lines]

    dfa = BenchHelper.compile_dfa(pattern)
    re  = Regexp.new(pattern)

    puts "\n--- #{bcase[:label]} ---"
    puts "    #{bcase[:description]}"
    puts "    lines: #{lines.length}"

    job = Benchmark.ips do |x|
      x.config(warmup: BenchHelper::WARMUP_SEC, time: BenchHelper::TIME_SEC)

      x.report('NarakuRuby::DFA#match?') { lines.each { |line| dfa.match?(line) } } if dfa
      x.report('Regexp#match?') { lines.each { |line| re.match?(line) } }

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
    accumulated << { 'label' => bcase[:label], 'pattern' => pattern, 'description' => bcase[:description], 'entries' => entries }
  end

  BenchHelper.write_results('corpus', accumulated)
end

run_corpus_benchmarks
