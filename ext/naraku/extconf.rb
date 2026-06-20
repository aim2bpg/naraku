require 'mkmf'

# Reuses the existing C core (src/*.c) compiled into build/libnaraku.a by
# `rake naraku:build_lib` instead of recompiling the sources here. Run that
# task before building this extension.
project_root = File.expand_path('../..', __dir__)
include_dir  = File.join(project_root, 'include')
lib_dir      = File.join(project_root, 'build')

unless File.exist?(File.join(lib_dir, 'libnaraku.a'))
  abort "build/libnaraku.a not found. Run `bundle exec rake naraku:build_lib` first."
end

$INCFLAGS << " -I#{include_dir}"
$LDFLAGS  << " -L#{lib_dir}"
$libs     << ' -lnaraku'

create_makefile('naraku_cext')
