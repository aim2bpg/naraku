# frozen_string_literal: true

require 'bundler/setup'
require 'minitest/test_task'
require 'rake/file_utils'

UNICODE_VERSION = '17.0.0'
MRUBY_CONFIG = ENV.fetch('MRUBY_CONFIG', '../../build_config.rb')
UNICODE_RUBY_SOURCES = FileList['tools/unicode/*.rb'].to_a.freeze

GENERATED_HEADER_RULES = {
  'include/naraku_cprop_names.h' => {
    deps: ['tools/gen-cprop-names.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen-cprop-names.rb --header #{UNICODE_VERSION} > include/naraku_cprop_names.h",
  },
  'src/.gen/name2cprop.gen.h' => {
    deps: ['tools/gen-cprop-names.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen-cprop-names.rb #{UNICODE_VERSION} > src/.gen/name2cprop.gen.h",
  },
  'src/.gen/cprop_range_ascii.gen.h' => {
    deps: ['tools/gen-cprop-range.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen-cprop-range.rb #{UNICODE_VERSION} --ascii > src/.gen/cprop_range_ascii.gen.h",
  },
  'src/.gen/case_map_ascii.gen.h' => {
    deps: ['tools/gen-case-map.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen-case-map.rb #{UNICODE_VERSION} --ascii > src/.gen/case_map_ascii.gen.h",
  },
  'src/.gen/cprop_range_unicode.gen.h' => {
    deps: ['tools/gen-cprop-range.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen-cprop-range.rb #{UNICODE_VERSION} --unicode > src/.gen/cprop_range_unicode.gen.h",
  },
  'src/.gen/case_map_unicode.gen.h' => {
    deps: ['tools/gen-case-map.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen-case-map.rb #{UNICODE_VERSION} --unicode > src/.gen/case_map_unicode.gen.h",
  },
  'src/encoding/.gen/cprop_range_iso_8859_1.gen.h' => {
    deps: ['tools/gen-cprop-range.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen-cprop-range.rb #{UNICODE_VERSION} --single-byte ISO-8859-1 --prefix iso_8859_1 > src/encoding/.gen/cprop_range_iso_8859_1.gen.h",
  },
  'src/encoding/.gen/case_map_iso_8859_1.gen.h' => {
    deps: ['tools/gen-case-map.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen-case-map.rb #{UNICODE_VERSION} --single-byte ISO-8859-1 --prefix iso_8859_1 > src/encoding/.gen/case_map_iso_8859_1.gen.h",
  },
  'src/encoding/.gen/cprop_range_shift_jis.gen.h' => {
    deps: ['tools/gen-cprop-range.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen-cprop-range.rb #{UNICODE_VERSION} --multi-byte2 Shift_JIS --prefix shift_jis > src/encoding/.gen/cprop_range_shift_jis.gen.h",
  },
  'src/encoding/.gen/case_map_shift_jis.gen.h' => {
    deps: ['tools/gen-case-map.rb', *UNICODE_RUBY_SOURCES],
    command: "ruby tools/gen-case-map.rb #{UNICODE_VERSION} --multi-byte2 Shift_JIS --prefix shift_jis > src/encoding/.gen/case_map_shift_jis.gen.h",
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

  desc 'Build mruby integration with ASan'
  task build_mruby_asan: [:clean_all] do
    sh(
      {
        'CFLAGS' => '-g -fsanitize=address',
        'LD' => 'clang',
        'LDFLAGS' => '-fsanitize=address',
      },
      'rake naraku:build_lib naraku:build_mruby'
    )
  end

  task :ensure_mruby do
    ensure_mruby_binary!
  end

  desc 'Run mruby test suite'
  task test_mruby: :ensure_mruby do
    sh 'bin/mruby tests/run_test.rb'
  end

  desc 'Run mruby test suite (verbose)'
  task test_mruby_verbose: :ensure_mruby do
    sh 'bin/mruby tests/run_test.rb -v'
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

namespace :ruby_prototype do
  desc 'Run ruby-prototype tests'
  Minitest::TestTask.create(:test) do |t|
    t.libs << 'ruby-prototype/tests'
    t.libs << 'ruby-prototype/lib'
    t.test_globs = ['ruby-prototype/tests/test_*.rb']
  end
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

desc 'Show available Rake tasks'
task :default do
  sh 'rake -T'
end
