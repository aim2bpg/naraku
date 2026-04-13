# frozen_string_literal: true

# This is a configuration file for building a MRuby binary.

MRuby::Build.new do |conf|
  conf.toolchain
  conf.gembox 'default'
  conf.gem core: 'mruby-exit'
  conf.gem github: 'buty4649/mruby-yyjson', branch: 'main'

  conf.gem File.expand_path('mrbgems/mruby-naraku', __dir__)

  conf.enable_bintest
  conf.enable_test
end
