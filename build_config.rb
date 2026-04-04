# frozen_string_literal: true

# This is a configuration file for building a MRuby binary.

MRuby::Build.new do |conf|
  conf.toolchain
  conf.gembox 'default'
  conf.gem :core => 'mruby-exit'
  conf.gem File.expand_path('../mrbgems/mruby-naraku', __FILE__)

  conf.enable_bintest
  conf.enable_test
end
