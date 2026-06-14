MRuby::Gem::Specification.new('mruby-naraku') do |spec|
  spec.license = 'BSD-2-Clause'
  spec.authors = 'makenowjust'

  naraku_root = File.expand_path('../..', __dir__)
  naraku_builddir = ENV.fetch('NARAKU_BUILDDIR', File.join(naraku_root, 'build'))

  spec.cc.include_paths << File.join(naraku_root, 'include')
  spec.linker.library_paths << naraku_builddir
  spec.linker.libraries << 'naraku'

  spec.cc
  original_get_dependencies = spec.cc.method(:get_dependencies)
  naraku_lib = File.join(naraku_builddir, 'libnaraku.a')
  spec.cc.define_singleton_method(:get_dependencies) do |file|
    deps = original_get_dependencies.call(file)
    deps << naraku_lib if file.end_with?('mrb_naraku.o')
    deps
  end
end
