# Pike VM benchmark — run under bin/mruby
# Outputs tab-separated lines to stdout:
#   RESULT  <label>  <ips>
#   SKIP    <label>  <reason>

# mruby has no built-in `require`/`load` and no `__dir__`; eval the shared
# CASES file in place (same pattern as test/test_run.rb's custom `require`).
bench_cases_path = File.expand_path('../ruby-prototype/benchmark/bench_cases.rb', File.dirname(__FILE__))
eval File.read(bench_cases_path), nil, bench_cases_path, 1 # rubocop:disable Security/Eval

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
