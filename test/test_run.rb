# frozen_string_literal: true

# This file is interpreted by mruby.

# First, we need to define methods that are not available in mruby.

REQUIRE_BASE_DIR = File.expand_path('../', __FILE__)

def require(path)
  absolute_path = File.expand_path(path, REQUIRE_BASE_DIR)
  absolute_path += '.rb' unless absolute_path.end_with?('.rb')

  eval File.read(absolute_path), nil, absolute_path, 1 # rubocop:disable Security/Eval
end

class Array
  def splice(index, length, *replacement)
    removed = self[index, length]
    self[index, length] = replacement
    removed
  end
end

# Loads the Mtest framework.

require 'mtest/mtest'

# Loads all test files.
# NOTE: mruby does not support `Dir.glob` by default.

def collect_test_files(dir = REQUIRE_BASE_DIR)
  test_files = []
  Dir.new(dir).each do |entry_name|
    next if entry_name.start_with?('.')

    entry_path = File.join(dir, entry_name)
    if File.directory?(entry_path)
      test_files.concat(collect_test_files(entry_path))
    elsif entry_name.end_with?('_test.rb')
      test_files << entry_path
    end
  end

  test_files
end

collect_test_files.each do |file|
  require file
end

Mtest.run(__FILE__, ARGV)
