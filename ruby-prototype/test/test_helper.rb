# frozen_string_literal: true

require 'bundler/setup'
require 'simplecov'
SimpleCov.start do
  add_filter '/test/'
  track_files 'ruby-prototype/lib/**/*.rb'
  root File.expand_path('../..', __dir__)
end

require 'minitest/autorun'
require 'minitest/reporters'

require 'naraku_ruby'

Minitest::Reporters.use!
