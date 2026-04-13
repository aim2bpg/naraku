# Copilot instructions for Naraku

Naraku is a Ruby/Onigmo-compatible regular expression engine implemented in C.

## Project structure

- `include/`
  - Public headers (`naraku_syntax.h`, `naraku_error.h`, encoding interfaces, node definitions).
- `src/`
  - Core implementation:
    - parser/lexer: `src/parse.c`
    - errors/warnings: `src/error.c`
    - encodings: `src/encoding/*.c`
    - node handling: `src/node.c`
- `mrbgems/mruby-naraku/`
  - mruby binding layer:
    - C bridge: `src/mrb_naraku*.c`
    - mruby wrapper classes/modules: `mrblib/naraku/*.rb`
- `tests/`
  - mruby-based tests (entrypoint: `tests/run_test.rb`)
  - custom framework: `Mtest`

## Current implementation status

- Implemented:
  - encoding layer (multiple encodings)
  - parser/lexer and AST construction
  - parser warning callback pipeline to mruby
- In progress:
  - regex VM/compiler prototyping in Ruby
- Not implemented yet:
  - production VM/compiler in C

## Build and test commands

- Normal build:
  - `make build-mruby`
- ASan build:
  - `make CFLAGS='-g -fsanitize=address' LD=clang LDFLAGS=-fsanitize=address clean build-mruby`
- Test:
  - `bin/mruby tests/run_test.rb`
  - verbose: `bin/mruby tests/run_test.rb -v`

## Parser/lexer implementation guidance

- Prefer Onigmo-compatible behavior unless divergence is explicitly requested.
- Error reporting quality is important:
  - set both `offset` and `length` (span),
  - use meaningful ranges when possible, not only point offsets.
- For new parser errors/warnings:
  - update enums in `include/naraku_error.h`,
  - add message in `src/error.c`,
  - add/adjust parser tests in `tests/test_parser.rb`.
- Keep explicit boundary checks at call sites (`parser->pattern_bytes < parser->pattern_bytes_end`) where context-specific errors are required.
- `peek()` includes a defensive end-of-pattern check as a fallback safety net.
- When adding parsing branches that inspect a “next token” (options/groups/char-class/escapes), ensure end-of-pattern is handled before `peek()`/`consume()`.

## mruby binding guidance

- `Naraku::Parser.new` options are wired through:
  - `mrblib/naraku/parser.rb` -> `_new`
  - `src/mrb_naraku_parser.c` -> `nk_parser_options_t`
- Keep Ruby Proc callbacks alive (e.g., warning callback stored on parser object) to avoid GC issues.
- Free C-side user data correctly in parser free hooks.
- Parse errors exposed to Ruby should preserve `offset`/`length` from parser error span fields.

## Testing expectations

- Add regression tests for every parser bug fix (especially span and boundary bugs).
- For warning behavior, test both message and `(offset, length)`.
- Keep tests mruby-compatible; do not assume CRuby-specific behavior.
