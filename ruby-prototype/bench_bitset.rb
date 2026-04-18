require 'benchmark/ips'

class BitSet
  def initialize
    @bits = 0
  end

  def include?(n) = @bits[n] == 1

  def add(n)
    @bits |= 1 << n
  end
end

bitset = BitSet.new
arr = Array.new(100, false)
n = 5

Benchmark.ips do |x|
  x.report('BitSet') do
    100.times do
      bitset.include?(n)
      bitset.add(n)
    end
  end
  x.report('Array') do
    100.times do
      arr[n]
      arr[n] = true
    end
  end
  x.compare!
end
