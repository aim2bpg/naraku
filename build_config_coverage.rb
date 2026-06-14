# frozen_string_literal: true

# Coverage build configuration for naraku C-layer gcov/lcov measurement.
# Uses a separate build name ("coverage") so it coexists with the normal "host" build.
# Run via: bundle exec rake naraku:coverage_c
MRuby::Build.new('coverage') do |conf|
  conf.toolchain
  conf.gembox 'default'
  conf.gem core: 'mruby-exit'
  conf.gem github: 'buty4649/mruby-yyjson', branch: 'main'

  conf.gem File.expand_path('mrbgems/mruby-naraku', __dir__)

  conf.cc.flags << '--coverage'
  conf.linker.flags << '--coverage'

  conf.enable_bintest
  conf.enable_test
end
