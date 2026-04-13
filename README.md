# Naraku

*Naraku* (**奈落** in Japanese) is the *regular expression engine* and the *encoding foundation* for the **Ruby** programming language.
It is a successor to Oniguruma (鬼車) and Onigmo (鬼雲).

> Q. *Why is the name Naraku?*\
> A. *Let's read **[Inuyasha](https://en.wikipedia.org/wiki/Inuyasha)***.
> <!-- spoiler: Naraku was Onigumo. -->

## Current status

**NOTE**: This project is in the early stages of development.

- Implemented: encoding layer (including multiple encodings) and parser.
- In progress: regular expression VM/compiler design and prototyping (currently in Ruby).
- Not yet implemented: production VM/compiler in C.

## Build

```sh
make build-mruby
```

### Build with AddressSanitizer

```sh
make CFLAGS='-g -fsanitize=address' LD=clang LDFLAGS=-fsanitize=address clean build-mruby
```

## Test

After building mruby:

```sh
bin/mruby tests/run_test.rb
```

Verbose mode:

```sh
bin/mruby tests/run_test.rb -v
```

### Notes about tests

- Tests are written in mruby (not CRuby).
- The test suite uses the project's own lightweight test framework, `Mtest`.
