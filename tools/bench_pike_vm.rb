# Pike VM benchmark — run under bin/mruby
# Outputs tab-separated lines to stdout:
#   RESULT  <label>  <ips>
#   SKIP    <label>  <reason>

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

def measure(label, re, inputs)
  t0 = Time.now
  inputs.each { |s| re.match?(s) } while Time.now - t0 < WARMUP_SEC

  count = 0
  t0 = Time.now
  while Time.now - t0 < MEASURE_SEC
    inputs.each { |s| re.match?(s) }
    count += 1
  end
  elapsed = Time.now - t0
  ips = count.to_f / elapsed

  puts "RESULT\t#{label}\t#{ips.round(2)}"
rescue StandardError => e
  puts "ERROR\t#{label}\t#{e.class}: #{e.message.lines.first.chomp}"
end

CASES.each do |c|
  re = Naraku::Regexp.new(c[:pattern])
  measure(c[:label], re, c[:inputs])
rescue Naraku::CompileError => e
  puts "SKIP\t#{c[:label]}\t#{e.message.lines.first.chomp}"
rescue StandardError => e
  puts "SKIP\t#{c[:label]}\t#{e.class}: #{e.message.lines.first.chomp}"
end
