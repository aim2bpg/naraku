# frozen_string_literal: true

require 'bundler/setup'
require 'fileutils'
require 'minitest/test_task'
require 'rake/file_utils'
require 'rbconfig'

UNICODE_VERSION = '17.0.0'
MRUBY_CONFIG = ENV.fetch('MRUBY_CONFIG', '../../build_config.rb')
UNICODE_RUBY_SOURCES = FileList['tools/unicode/*.rb'].to_a.freeze

GENERATED_HEADER_RULES = {
  'include/naraku_cprop_names.h' => {
    deps: ['tools/gen_cprop_names.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen_cprop_names.rb --header #{UNICODE_VERSION} > include/naraku_cprop_names.h",
  },
  'src/.gen/name2cprop.gen.h' => {
    deps: ['tools/gen_cprop_names.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen_cprop_names.rb #{UNICODE_VERSION} > src/.gen/name2cprop.gen.h",
  },
  'src/.gen/cprop_range_ascii.gen.h' => {
    deps: ['tools/gen_cprop_range.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen_cprop_range.rb #{UNICODE_VERSION} --ascii > src/.gen/cprop_range_ascii.gen.h",
  },
  'src/.gen/case_map_ascii.gen.h' => {
    deps: ['tools/gen_case_map.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen_case_map.rb #{UNICODE_VERSION} --ascii > src/.gen/case_map_ascii.gen.h",
  },
  'src/.gen/cprop_range_unicode.gen.h' => {
    deps: ['tools/gen_cprop_range.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen_cprop_range.rb #{UNICODE_VERSION} --unicode > src/.gen/cprop_range_unicode.gen.h",
  },
  'src/.gen/case_map_unicode.gen.h' => {
    deps: ['tools/gen_case_map.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen_case_map.rb #{UNICODE_VERSION} --unicode > src/.gen/case_map_unicode.gen.h",
  },
  'src/encoding/.gen/cprop_range_iso_8859_1.gen.h' => {
    deps: ['tools/gen_cprop_range.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen_cprop_range.rb #{UNICODE_VERSION} --single-byte ISO-8859-1 --prefix iso_8859_1 > src/encoding/.gen/cprop_range_iso_8859_1.gen.h",
  },
  'src/encoding/.gen/case_map_iso_8859_1.gen.h' => {
    deps: ['tools/gen_case_map.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen_case_map.rb #{UNICODE_VERSION} --single-byte ISO-8859-1 --prefix iso_8859_1 > src/encoding/.gen/case_map_iso_8859_1.gen.h",
  },
  'src/encoding/.gen/cprop_range_shift_jis.gen.h' => {
    deps: ['tools/gen_cprop_range.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen_cprop_range.rb #{UNICODE_VERSION} --multi-byte2 Shift_JIS --prefix shift_jis > src/encoding/.gen/cprop_range_shift_jis.gen.h",
  },
  'src/encoding/.gen/case_map_shift_jis.gen.h' => {
    deps: ['tools/gen_case_map.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen_case_map.rb #{UNICODE_VERSION} --multi-byte2 Shift_JIS --prefix shift_jis > src/encoding/.gen/case_map_shift_jis.gen.h",
  },
}.freeze

GENERATED_HEADERS = GENERATED_HEADER_RULES.keys.freeze

def ensure_mruby_binary!
  path = 'bin/mruby'

  raise "mruby binary is missing: #{path}. Run `bundle exec rake naraku:build_mruby` first." unless File.exist?(path)

  return unless File.symlink?(path)

  link_target = File.expand_path(File.readlink(path), File.dirname(path))
  return if File.exist?(link_target)

  raise "mruby symlink target is missing: #{link_target}. Rebuild with `bundle exec rake naraku:build_mruby`."
end

GENERATED_HEADER_RULES.each do |target, rule|
  file target => rule[:deps] do
    mkdir_p File.dirname(target)
    sh(rule[:command])
  end
end

ASAN_ENV = {
  'CFLAGS' => '-g -fsanitize=address',
  'LD' => 'clang',
  'LDFLAGS' => '-fsanitize=address',
}.freeze

def generate_c_coverage_report
  root = File.expand_path('.')
  mkdir_p 'coverage'
  mkdir_p 'coverage/gcov_tmp'
  sh <<~SH.strip
    lcov --capture \
      --directory build/coverage/static \
      --directory submodules/mruby/build/coverage/mrbgems/mruby-naraku \
      --base-directory #{root} \
      --tempdir coverage/gcov_tmp \
      --rc geninfo_unexecuted_blocks=1 \
      --output-file coverage/naraku_c_raw.info
  SH
  sh <<~SH.strip
    lcov \
      --extract coverage/naraku_c_raw.info \
      '#{root}/src/*' \
      '#{root}/mrbgems/mruby-naraku/src/*' \
      --output-file coverage/naraku_c.info
  SH
  sh "genhtml coverage/naraku_c.info --output-directory coverage/c_html --title 'Naraku C Coverage'"
  sh 'lcov --summary coverage/naraku_c.info'
  puts "\nHTML report: coverage/c_html/index.html"
end

namespace :naraku do
  desc 'Generate Unicode/cprop headers'
  task codegen: GENERATED_HEADERS

  desc 'Build libnaraku.a (C build)'
  task build_lib: :codegen do
    sh 'make build/libnaraku.a'
  end

  desc 'Build mruby integration'
  task build_mruby: :build_lib do
    sh "cd submodules/mruby && rake MRUBY_CONFIG=#{MRUBY_CONFIG}"
  end

  desc 'Build naraku C files with gcov instrumentation (into build/coverage/)'
  task build_lib_coverage: :codegen do
    sh 'make build/coverage/libnaraku.a'
  end

  desc 'Build mruby with gcov instrumentation (coverage named build, separate from host build)'
  task build_mruby_coverage: :build_lib_coverage do
    sh(
      { 'NARAKU_BUILDDIR' => File.expand_path('build/coverage') },
      "cd submodules/mruby && rake MRUBY_CONFIG=#{File.expand_path('build_config_coverage.rb')}"
    )
  end

  desc 'Run C-layer coverage: build (gcov) → test → lcov HTML report in coverage/c_html/'
  task coverage_c: :build_mruby_coverage do
    sh "#{File.expand_path('submodules/mruby/build/coverage/bin/mruby')} test/test_run.rb"
    generate_c_coverage_report
  end

  desc 'Build mruby integration with ASan'
  task build_mruby_asan: [:clean_all] do
    sh(ASAN_ENV, 'rake naraku:build_lib naraku:build_mruby')
  end

  task :ensure_mruby do
    ensure_mruby_binary!
  end

  desc 'Run mruby test suite'
  task test_mruby: :ensure_mruby do
    sh 'bin/mruby test/test_run.rb'
  end

  desc 'Run mruby test suite (verbose)'
  task test_mruby_verbose: :ensure_mruby do
    sh 'bin/mruby test/test_run.rb -v'
  end

  desc 'Format C source files'
  task :format do
    sh 'make format'
  end

  desc 'Clean C build artifacts'
  task :clean do
    sh 'make clean'
  end

  desc 'Clean mruby build artifacts'
  task :clean_mruby do
    sh 'cd submodules/mruby && rake clean'
  end

  desc 'Clean C and mruby build artifacts'
  task clean_all: %i[clean clean_mruby]
end

def ruby_prototype_coverage_script(test_files)
  requires = test_files.sort.map { |f| "require '#{File.expand_path(f)}'" }.join("\n  ")
  <<~RUBY
    require 'simplecov'
    SimpleCov.start do
      add_filter '/test/'
      add_filter 'mruby-scripts'
      track_files 'ruby-prototype/lib/**/*.rb'
      root '#{__dir__}'
    end
    require 'minitest/autorun'
    require 'minitest/reporters'
    Minitest::Reporters.use!
    #{requires}
  RUBY
end

BENCHMARK_DIR = 'ruby-prototype/benchmark'
BENCHMARK_RESULTS_DIR = "#{BENCHMARK_DIR}/results".freeze

BENCHMARK_RUBY_FLAGS = {
  'plain' => [],
  'yjit' => ['--yjit'],
  'zjit' => ['--zjit'],
}.freeze

BENCHMARK_SUITES = %w[synthetic corpus].freeze

namespace :ruby_prototype do
  desc 'Run ruby-prototype tests'
  Minitest::TestTask.create(:test) do |t|
    t.libs << 'ruby-prototype/test'
    t.libs << 'ruby-prototype/lib'
    t.test_globs = ['ruby-prototype/test/**/*_test.rb']
  end

  desc 'Run ruby-prototype tests with line coverage (SimpleCov, single process)'
  task :coverage do
    test_files = Dir['ruby-prototype/test/**/*_test.rb']
    ruby "-Iruby-prototype/test -Iruby-prototype/lib -e #{ruby_prototype_coverage_script(test_files).shellescape}"
  end

  namespace :benchmark do
    # ── JIT モードごとの単独タスク ─────────────────────────────
    BENCHMARK_RUBY_FLAGS.each do |mode, flags|
      desc "Run all benchmark suites with #{mode.upcase}"
      task mode.to_sym do
        ruby_bin = RbConfig.ruby
        FileUtils.mkdir_p(BENCHMARK_RESULTS_DIR)
        FileUtils.rm_f("#{BENCHMARK_RESULTS_DIR}/#{mode}.json")

        BENCHMARK_SUITES.each do |suite|
          bench_file = "#{BENCHMARK_DIR}/bench_#{suite}.rb"
          sh([ruby_bin, *flags, bench_file].join(' '))
        end
      end
    end

    # ── レポート生成のみ ──────────────────────────────────────
    desc 'Generate Markdown report from existing JSON results'
    task :report do
      sh "#{RbConfig.ruby} #{BENCHMARK_DIR}/generate_report.rb"
    end

    desc 'Profile DFA matcher hot paths (stackprof + memory_profiler)'
    task :profile do
      sh "#{RbConfig.ruby} #{BENCHMARK_DIR}/profile_dfa.rb"
    end

    # ── 個別スイート用 (追加の柔軟性のため) ──────────────────
    BENCHMARK_SUITES.each do |suite|
      namespace suite.to_sym do
        BENCHMARK_RUBY_FLAGS.each do |mode, flags|
          desc "Run #{suite} benchmark suite with #{mode.upcase}"
          task mode.to_sym do
            ruby_bin = RbConfig.ruby
            FileUtils.mkdir_p(BENCHMARK_RESULTS_DIR)
            sh([ruby_bin, *flags, "#{BENCHMARK_DIR}/bench_#{suite}.rb"].join(' '))
          end
        end
      end
    end
  end

  desc 'Run all benchmarks (plain + yjit + zjit) and generate report'
  task benchmark: %w[
    ruby_prototype:benchmark:plain
    ruby_prototype:benchmark:yjit
    ruby_prototype:benchmark:zjit
    ruby_prototype:benchmark:report
  ]
end

namespace :ruby do
  desc 'Lint Ruby files with RuboCop'
  task :lint do
    sh 'bundle exec rubocop'
  end

  desc 'Auto-correct Ruby files with RuboCop'
  task :format do
    sh 'bundle exec rubocop -A'
  end
end

desc 'Lint source files'
task lint: 'ruby:lint'

desc 'Format C and Ruby source files'
task format: ['naraku:format', 'ruby:format']

desc 'Install git hooks from .hooks/'
task :install_hooks do
  Dir.glob('.hooks/*').each do |hook|
    dest = ".git/hooks/#{File.basename(hook)}"
    cp hook, dest
    chmod 0o755, dest
    puts "Installed #{dest}"
  end
end

desc 'Show available Rake tasks'
task :default do
  sh 'rake -T'
end
