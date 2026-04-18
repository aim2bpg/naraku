require_relative 'benchmark/bench_helper'
dfa = BenchHelper.compile_dfa("(a|a)*x")
dfa.show_states
