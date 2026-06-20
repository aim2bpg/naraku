# CLAUDE.md — Naraku Project Guide

Naraku (**奈落**) is a Ruby-dedicated regular expression engine written in C, aimed to be a successor to Oniguruma (鬼車) and Onigmo (鬼雲).

## Current implementation status

- **Implemented**: encoding layer (multiple encodings), parser/lexer, AST construction, parser warning callback pipeline to mruby
- **In progress**: regex VM/compiler prototyping in Ruby (`ruby-prototype/`)
- **Not yet implemented**: production VM/compiler in C

## Project structure

```
include/                    Public C headers
src/                        Core C implementation
  parse.c                   Parser/lexer (main file, ~4300 lines)
  postprocess.c             AST postprocessing
  encoding_ascii.c          ASCII encoding logic
  encoding_unicode.c        Unicode encoding logic
  encoding/                 Per-encoding implementations (utf_8, shift_jis, etc.)
mrbgems/mruby-naraku/       mruby binding layer
  src/mrb_naraku*.c         C bridge
  mrblib/naraku/*.rb        mruby wrapper classes
ext/naraku/                 CRuby native extension (benchmarking only)
ruby-prototype/             Ruby-side VM/compiler prototyping
  lib/naraku_ruby/          CRuby helper and DFA prototype
  lib/mruby-scripts/        Scripts executed by mruby
  test/                     Prototype tests (run via top-level Rake task)
test/                       mruby-based C layer tests (entrypoint: test/test_run.rb)
tools/                      Code generators (Unicode/cprop headers via gperf)
submodules/mruby/           mruby submodule
```

## Build prerequisites

System packages required (Ubuntu/Debian):

```sh
sudo apt-get install -y \
  build-essential bison clang clang-format gperf \
  libssl-dev libreadline-dev zlib1g-dev \
  libffi-dev libyaml-dev libgmp-dev \
  lcov
```

**`gperf` is required** for generating Unicode character property lookup tables. Omitting it causes a silent build failure during `naraku:codegen`.

**`clang` is required** for `naraku:build_mruby_asan` (the AddressSanitizer build), which links with `LD=clang`. Without it, the task fails with `clang: not found`.

**`lcov` is required** for the C coverage tasks (`naraku:coverage_c`). It is installed by `setup.sh`.

Ruby is managed via **rbenv**. The required version is in `.ruby-version`.

## Setup (Dev Container)

The Dev Container handles everything automatically:

```sh
bash .devcontainer/setup.sh
```

This installs system packages, rbenv, Ruby, gems, downloads the Unicode Character Database (UCD 17.0.0), and builds mruby.

### Claude Code (optional)

To install [Claude Code](https://claude.ai/code) inside the Dev Container, create a
marker file at the workspace root before (re)building:

```sh
touch .install-claude-code
```

The file is gitignored and scoped to this clone, so it won't affect other
projects or other checkouts. `setup.sh` checks for it and installs Claude Code
if present.

**Host (no Dev Container):** install directly:

```sh
curl -fsSL https://claude.ai/install.sh | bash
```

### GitHub CLI (gh)

`gh` is installed automatically. Authenticate once after the container is created:

```sh
gh auth login
```

Follow the device flow — choose **SSH** as the Git protocol and **Login with a web browser**. Open `https://github.com/login/device`, enter the one-time code, and authentication completes. Verify with `gh auth status`.

Credentials are stored in `~/.config/gh/hosts.yml`, which is bind-mounted from the host, so they survive container rebuilds.

## Build and test commands

```sh
bundle exec rake -T                    # List all tasks

bundle exec rake naraku:build_mruby    # Build mruby integration (full C + mruby build)
bundle exec rake naraku:build_cruby_ext # Build ext/naraku/ (CRuby native ext, for benchmarking against Onigmo)
bundle exec rake naraku:test_mruby     # Run mruby parser/encoding tests
bundle exec rake naraku:test_mruby_verbose  # Verbose test output

bundle exec rake ruby_prototype:test   # Run Ruby prototype tests (no C build needed)

bundle exec rake lint                  # RuboCop lint
bundle exec rake format                # clang-format + RuboCop autocorrect

# Coverage measurement (requires lcov system package):
bundle exec rake naraku:coverage_c     # C layer: build (gcov) → test → lcov HTML in coverage/c_html/
bundle exec rake ruby_prototype:coverage  # Ruby prototype: SimpleCov line coverage report in coverage/
```

## Development process

### Commit messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
fix:   bug fix
feat:  new feature
chore: tooling, config, maintenance (no production code change)
```

Examples from this project: `fix: correct report generator`, `feat: add benchmark`, `chore: format Rakefile`

### Pre-commit hook

A pre-commit hook (`.hooks/pre-commit`) runs lint and secret scanning automatically on every `git commit`.

Install it once:

```sh
bundle exec rake install_hooks   # works without Dev Container too
```

The Dev Container installs it automatically. If lint fails, fix with:

```sh
bundle exec rake format
```

## Parser/lexer guidance

- Prefer Onigmo-compatible behavior unless divergence is explicitly requested.
- Error reporting: always set both `offset` and `length` (span), not just a point offset.
- For new parser errors/warnings:
  - Update enums in `include/naraku_error.h`
  - Add message in `src/error.c`
  - Add/adjust tests in `test/test_parser.rb`
- Keep explicit boundary checks at call sites where context-specific errors are needed.
- `peek()` has a defensive end-of-pattern check as a safety net.
- When adding branches that inspect a "next token", handle end-of-pattern before `peek()`/`consume()`.

## mruby binding guidance

- `Naraku::Parser.new` options flow: `mrblib/naraku/parser.rb` → `_new` → `src/mrb_naraku_parser.c` → `nk_parser_options_t`
- In `bin/mruby`, `Naraku` is built-in; do not `require 'naraku'`.
- Keep Ruby Proc callbacks alive (e.g., warning callback stored on parser object) to avoid GC.
- Free C-side user data correctly in parser free hooks.
- Parse errors exposed to Ruby should preserve `offset`/`length` from parser error span fields.

## Code generation (tools/)

Unicode character property headers are generated via `gperf`. The generators post-process gperf output to fix compatibility with the project's strict compiler flags (`-Wall -Werror -Wextra -Wconversion`):

- `gen_cprop_names.rb`: fixes `unsigned int hval = len` conversion (gperf 3.1 / 64-bit issue)
- `gen_case_map.rb`: fixes unused `len` parameter and `asso_values` sign-conversion warnings
- `gen_cprop_range.rb`: avoids generating `code >= 0x0` for `uint32_t` types

If codegen breaks after a gperf version upgrade, check the post-processing gsub chains in these files.

## Compiler flags

`-std=c99 -Wall -Werror -Wextra -Wpedantic -Wundef -Wconversion -Wno-missing-braces -fPIC -fvisibility=hidden -Wimplicit-fallthrough`

Strict flags are intentional. Generated headers are post-processed to comply rather than suppressing warnings at the include site.

## Testing expectations

- Add regression tests for every parser bug fix (especially span and boundary bugs).
- For warning behavior, test both message and `(offset, length)`.
- Tests must be mruby-compatible; do not assume CRuby-specific behavior.
- The test framework is `Mtest` (project's own lightweight framework for mruby).
