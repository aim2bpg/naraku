require 'benchmark'
require_relative 'benchmark/bench_helper'

s = "a" * 28 + "b"
re = BenchHelper.compile_dfa("(a|a)*x")
puts Benchmark.measure { re.match?(s) }
