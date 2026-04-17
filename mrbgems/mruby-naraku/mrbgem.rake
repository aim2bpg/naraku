MRuby::Gem::Specification.new('mruby-naraku') do |spec|
  spec.license = 'BSD-2-Clause'
  spec.authors = 'makenowjust'

  spec.cc.include_paths << File.expand_path('../../include', __dir__)
  spec.linker.library_paths << File.expand_path('../../build', __dir__)
  spec.linker.libraries << 'naraku'

  spec.cc
  original_get_dependencies = spec.cc.method(:get_dependencies)
  spec.cc.define_singleton_method(:get_dependencies) do |file|
    deps = original_get_dependencies.call(file)
    deps << File.expand_path('../../build/libnaraku.a', __dir__) if file.end_with?('mrb_naraku.o')
    deps
  end
end
