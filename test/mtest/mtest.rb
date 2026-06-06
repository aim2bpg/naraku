# frozen_string_literal: true

# `Mtest` is a testing framework, a port of `Minitest` to mruby.

module Mtest
  # The version constant.
  VERSION = '0.1.0'

  # Shell special characters that need to be escaped when included in command-line arguments.
  SHELL_SPECIAL_CHARS = " \t|&<>$()".chars

  # Converts an absolute path to a relative path from the current working directory if it is under it.
  def self.relative_path(path)
    path.delete_prefix("#{Dir.pwd}/")
  end

  OPTIONS = <<~HELP
    Options:

      -h, --help                    Show this help message and exit
      -V, --version                 Show Mtest version and exit
      -v, --verbose                 Run with verbose output
      -s, --seed SEED               Set seed for RNG (integer)
      -q, --quiet                   Run with minimal output
      --show-skips                  Show skipped tests in the summary report
      -i, --include STRING          Only run tests whose full name includes STRING
      -n, --name STRING             Alias for --include
      -e, -x, --exclude STRING      Exclude tests whose full name includes STRING
      -S, --skips CHARS             Skip tests with result codes in CHARS (e.g. "FS" to skip failures and skips)
      --no-filter-backtrace         Show full backtrace without filtering out Mtest internals
  HELP

  # Parses command-line arguments and returns a hash of option values.
  def self.process_args(cmd_path, args)
    original_args = args.dup

    options = {
      verbose: false,
      seed: nil,
      quiet: false,
      show_skips: false,
      include: nil,
      exclude: nil,
      skips: [],
      filter_backtrace: true,
    }

    i = 0
    while i < args.size
      if args[i].start_with?('--') && args[i].include?('=')
        key, value = args[i].split('=', 2)
        args.splice(i, 1, key, value)
      end

      if args[i].start_with?('-') && !args[i].start_with?('--')
        flag = 'sinexS'
               .chars
               .map { |flag| [flag, args[i].index(flag)] }
               .filter { |_, index| index }
               .min_by { |_, index| index }
               &.first
        if flag
          key, value = args[i].split(flag, 2)
          args.splice(i, 1, "#{key}#{flag}", value) unless value.empty?
        end

        short_flags = args.splice(i, 1)[0][1..]
        short_flags.chars.each_with_index do |flag, index|
          args.insert(i + index, "-#{flag}")
        end
      end

      case args[i]
      when '-h', '--help'
        puts "Usage: bin/mruby #{relative_path(cmd_path)} [options]"
        puts
        puts OPTIONS
        exit 0
      when '-V', '--version'
        puts "MTest #{Mtest::VERSION}"
        exit 0
      when '-v', '--verbose'
        options[:verbose] = true
      when '-s', '--seed'
        i += 1
        options[:seed] = args[i].to_i
      when '-q', '--quiet'
        options[:quiet] = true
      when '--show-skips'
        options[:show_skips] = true
      when '-i', '-n', '--name', '--include'
        i += 1
        options[:include] = args[i]
      when '-e', '-x', '--exclude'
        i += 1
        options[:exclude] = args[i]
      when '-S', '--skips'
        i += 1
        options[:skips] = args[i].chars
      when '--no-filter-backtrace'
        options[:filter_backtrace] = false
      end

      i += 1
    end

    unless options[:seed]
      srand
      options[:seed] = srand.to_i % 0xFFFF
      original_args << '--seed' << options[:seed].to_s
    end

    options[:args] = original_args.map do |arg|
      (arg.chars & SHELL_SPECIAL_CHARS) == [] ? arg : arg.inspect
    end

    options
  end

  def self.run(cmd_path, args)
    options = process_args(cmd_path, args)

    srand options[:seed]

    reporters = Reporter.reporters
    reporters.each { |reporter| reporter.options = options }

    reporters.each(&:on_start)

    Test.test_classes.each do |test_class|
      test_class.test_methods.each do |test_method|
        next unless included_test_name?(options, test_class.name, test_method)

        reporters.each do |reporter|
          reporter.on_test_start(test_class.name, test_method)
        end

        test = test_class.new(test_method)
        result = test.run
        reporters.each do |reporter|
          reporter.on_test_finish(result)
        end
      end
    end

    reporters.each(&:on_finish)
  end

  def self.included_test_name?(options, class_name, method_name)
    full_name = "#{class_name}##{method_name}"

    return false if options[:include] && !full_name.include?(options[:include])

    return false if options[:exclude] && full_name.include?(options[:exclude])

    true
  end

  def self.filter_backtrace(options, backtrace)
    return backtrace unless options[:filter_backtrace]

    backtrace
      .drop_while { |line| line.include?('mtest.rb:') }
      .take_while { |line| !line.include?('mtest.rb:') }
  end

  def self.time
    Time.now.to_f
  end

  class Assertion < StandardError
    def error
      self
    end

    def filtered_backtrace(options)
      Mtest.filter_backtrace(options, error.backtrace)
    end

    def result_code
      result_label[0]
    end

    def result_label
      'FAIL'
    end
  end

  class Skip < Assertion
    def result_label
      'SKIP'
    end
  end

  class UnexpectedError < Assertion
    def initialize(error)
      super(error.message)
      @error = error
    end

    attr_reader :error

    def result_label
      'ERROR'
    end
  end

  module Assertions
    def skip(message = nil)
      message ||= 'Skipped'
      raise Skip, message
    end

    def assert(test, message = nil)
      @num_assertions += 1
      return if test

      message ||= "Expected #{test.inspect} to be truthy"
      raise Assertion, message
    end

    def assert_equal(expected, actual, message = nil)
      message ||= "Expected #{actual.inspect} to be equal to #{expected.inspect}"
      assert(expected == actual, message)
    end

    def assert_nil(object, message = nil)
      message ||= "Expected #{object.inspect} to be nil"
      assert(object.nil?, message)
    end

    def assert_raises(expected_error_class, expected_message = nil, message = nil, &)
      @num_assertions += 1

      message ||= begin
        m = "Expected #{expected_error_class} to be raised"
        m + " with message #{expected_message.inspect}" if expected_message
      end

      begin
        yield
        raise Assertion, "#{message}\nBut, no error was raised"
      rescue expected_error_class => e
        if expected_message && !e.message.include?(expected_message)
          raise Assertion, "#{message}\nBut, the raised error is #{e.class} with message #{e.message.inspect}"
        end
      rescue Mtest::Assertion, NoMemoryError, SystemExit => e
        raise e
      rescue Exception => e # rubocop:disable Lint/RescueException
        raise Assertion, "#{message}\nBut, the raised error is #{e.class} with message #{e.message.inspect}"
      end
    end

    # TODO: Rewrite `assert_timeout` as `assert_linear_time` that asserts the block runs in linear time with
    # respect to the input size, by running the block with different input sizes and checking the time differences.

    def assert_timeout(seconds, message = nil, &)
      message ||= "Expected block to finish within #{seconds} seconds"

      start_time = Mtest.time
      yield
      end_time = Mtest.time

      elapsed = end_time - start_time
      assert(elapsed <= seconds, "#{message}\nBut, it took #{elapsed.round(2)} seconds")
    end
  end

  class Test
    include Assertions

    class << self
      def inherited(subclass)
        super
        @test_classes ||= []
        @test_classes << subclass
      end

      def test_classes
        @test_classes ||= []
        @test_classes.shuffle
      end
    end

    def initialize(method_name)
      @method_name = method_name
      @time = nil
      @num_assertions = 0
      @failures = []
    end

    attr_reader :method_name, :time, :num_assertions, :failures

    def self.test_methods
      methods = public_instance_methods(false)
                .map(&:to_s)
                .filter { |m| m.start_with?('test_') }

      methods.shuffle
    end

    def run
      t0 = Mtest.time
      capture_exceptions do
        setup
        send(method_name)
      end

      capture_exceptions { teardown }

      @time = Mtest.time - t0

      Result.from(self)
    end

    def setup
      # no-op
    end

    def teardown
      # no-op
    end

    def capture_exceptions(&)
      yield
    rescue NoMemoryError, SystemExit
      raise
    rescue Assertion => e
      @failures << e
    rescue Exception => e # rubocop:disable Lint/RescueException
      @failures << UnexpectedError.new(e)
    end
  end

  class Result
    RESULT_CODE_COLORS = {
      '.' => "\e[32m",    # green
      'F' => "\e[31m",    # red
      'S' => "\e[33m",    # yellow
      'E' => "\e[37;41m", # white on red
    }.freeze

    def initialize(class_name, method_name, time, num_assertions, failures)
      @class_name = class_name
      @method_name = method_name
      @time = time
      @num_assertions = num_assertions
      @failures = failures
    end

    attr_reader :class_name, :method_name, :time, :num_assertions, :failures

    def self.from(test)
      new(
        test.class.name,
        test.method_name,
        test.time,
        test.num_assertions,
        test.failures
      )
    end

    def failure
      failures.first
    end

    def result_code
      failure&.result_code || '.'
    end

    def colored_result_code
      color = RESULT_CODE_COLORS[result_code] || ''
      color_end = color == '' ? '' : "\e[0m"
      "#{color}#{result_code}#{color_end}"
    end

    def result_label
      failure&.result_label || 'PASS'
    end

    def colored_result_label
      color = RESULT_CODE_COLORS[result_code] || ''
      color_end = color == '' ? '' : "\e[0m"
      "#{color}#{result_label}#{color_end}"
    end
  end

  class Reporter
    class << self
      def register(reporter)
        @reporters ||= []
        @reporters << reporter
      end

      def reporters
        @reporters ||= []
        @reporters
      end
    end

    def initialize
      # `options` will be set in `Mtest.run`.
      @options = nil
    end

    attr_accessor :options

    def on_start
      # no-op
    end

    def on_test_start(class_name, method_name)
      # no-op
    end

    def on_test_finish(result)
      # no-op
    end

    def on_finish
      # no-op
    end
  end

  class ProgressReporter < Reporter
    def initialize(io)
      super()

      @io = io
      @num_wrote_dots = 0
    end

    def show_dots?
      !options[:quiet] && !options[:verbose]
    end

    def verbose?
      options[:verbose]
    end

    # Non-interactive outputs (e.g. CI logs, redirected files) don't process
    # backspace as a cursor movement, so the `*` placeholder used to animate
    # progress on a terminal would stay visible alongside the final result
    # character instead of being overwritten by it.
    def interactive?
      @io.respond_to?(:tty?) && @io.tty?
    end

    def on_test_start(class_name, method_name)
      if show_dots? && interactive?
        @io.print('*')
        @io.flush
      end

      return unless verbose?

      @io.print "#{class_name}##{method_name} ..."
      @io.flush
    end

    def on_test_finish(result)
      if show_dots?
        @io.print(interactive? ? "\b#{result.colored_result_code}" : result.colored_result_code)
        @num_wrote_dots += 1
        if (@num_wrote_dots % 100).zero?
          @io.puts
        else
          @io.flush
        end
      end

      return unless verbose?

      @io.puts "\r#{result.class_name}##{result.method_name} ... #{result.colored_result_label}\n"

      return unless result.failure

      @io.puts
      result.failures.each do |failure|
        @io.puts "  #{failure.error.class}: #{failure.error.message.split("\n").join("\n  ")}"
        failure.filtered_backtrace(options).each do |line|
          @io.puts "    #{line}"
        end
      end
      @io.puts
    end

    def on_finish
      return unless show_dots? && @num_wrote_dots.positive? && @num_wrote_dots % 100 != 0

      @io.puts
    end
  end

  class SummaryReporter < Reporter
    def initialize(io)
      super()

      @io = io

      @num_tests = 0
      @num_assertions = 0
      @num_skips = 0
      @start_time = nil
      @reports = []
    end

    def report_skips?
      options[:show_skips] || options[:verbose]
    end

    def on_start
      @start_time = Mtest.time

      @io.puts "Run options: #{options[:args].join(' ')}"
      @io.puts
      @io.puts 'Running:'
      @io.puts
    end

    def on_test_finish(result)
      @num_tests += 1
      @num_assertions += result.num_assertions

      case result.result_code
      when 'F', 'E'
        @reports << result
      when 'S'
        @reports << result if report_skips?
        @num_skips += 1
      end
    end

    def on_finish
      total_time = Mtest.time - @start_time

      @io.puts

      unless @reports.empty?
        if report_skips?
          @io.puts 'Failure/Error/Skip Reports:'
        else
          @io.puts 'Failure/Error Reports:'
        end

        @io.puts
        @reports.each_with_index do |result, index|
          next if options[:skips].include?(result.result_code)

          @io.puts "#{(index + 1).to_s.rjust(2)}) #{result.class_name}##{result.method_name} ... #{result.colored_result_label}"
          result.failures.each do |failure|
            @io.puts
            @io.puts "  #{failure.error.class}: #{failure.error.message.split("\n").join("\n  ")}"
            failure.filtered_backtrace(options).each do |line|
              @io.puts "    #{line}"
            end
          end
          @io.puts
        end
      end

      @io.puts "Finished in #{total_time.round(2)} seconds"
      num_fails = @reports.count { |r| r.result_code == 'F' }
      num_errors = @reports.count { |r| r.result_code == 'E' }
      @io.puts "#{@num_tests} tests, #{@num_assertions} assertions, #{num_fails} failures, #{num_errors} errors, #{@num_skips} skips"
    end
  end

  Reporter.register(ProgressReporter.new($stdout))
  Reporter.register(SummaryReporter.new($stdout))
end
