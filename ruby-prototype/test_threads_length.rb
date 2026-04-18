require_relative 'benchmark/bench_helper'

s = "a" * 28 + "b"
re = BenchHelper.compile_dfa("(a|a)*x")
puts re.program.run(s).inspect

class NarakuRuby::DFA::Program
  alias_method :original_run, :run
  def run(string, start_pos = 0)
    puts "RUNNING!"
    original_run(string, start_pos)
  end
end
