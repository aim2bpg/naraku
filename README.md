# Naraku

[![CI](https://github.com/aim2bpg/naraku/actions/workflows/ci.yml/badge.svg)](https://github.com/aim2bpg/naraku/actions/workflows/ci.yml)

*Naraku* (**奈落** in Japanese) is the **Ruby-dedicated** regular expression engine.
It is aimed to be a successor to Oniguruma (鬼車) and Onigmo (鬼雲).

> Q. *Why is the name Naraku?*\
> A. *Let's read **[Inuyasha](https://en.wikipedia.org/wiki/Inuyasha)***.
> <!-- spoiler: Naraku was Onigumo. -->

## Current status

**NOTE**: This project is in the early stages of development.

- Implemented: encoding layer (including multiple encodings) and parser.
- In progress: regular expression VM/compiler design and prototyping (currently in Ruby).
- Not yet implemented: production VM/compiler in C.

## Development

### Quick start with Dev Container

If you use VS Code, you can get a fully configured environment in one step:

1. Open the repository in VS Code
2. Click **"Reopen in Container"**

Ruby, mruby, and all dependencies are installed automatically.

To also install [Claude Code](https://claude.ai/code), create a marker file at
the workspace root before (re)building the container:

```sh
touch .install-claude-code
```

The file is gitignored and scoped to this clone, so it won't affect other
projects or checkouts.

### Manual setup

Install system dependencies:

```sh
sudo apt-get install -y build-essential bison clang gperf
```

Install git hooks (runs lint automatically on commit):

```sh
bundle exec rake install_hooks
```

Then follow the [Build](#build) and [Test](#test) sections below.

## Build

Primary task runner:

```sh
bundle exec rake -T
```

Build mruby integration:

```sh
bundle exec rake naraku:build_mruby
```

### Build with AddressSanitizer

```sh
bundle exec rake naraku:build_mruby_asan
```

## Test

Run mruby parser tests:

```sh
bundle exec rake naraku:test_mruby
```

Verbose mode:

```sh
bundle exec rake naraku:test_mruby_verbose
```

### Notes about tests

- Tests are written in mruby (not CRuby).
- The test suite uses the project's own lightweight test framework, `Mtest`.

## Ruby prototype

- Prototype code is placed under `ruby-prototype/`.
  - Library code: `ruby-prototype/lib`
  - Tests: `ruby-prototype/test`

Bundler setup (project root):

```sh
bundle install
```

Run Ruby prototype tests:

```sh
bundle exec rake ruby_prototype:test
```

Run DFA profiling (outputs under `ruby-prototype/benchmark/results/profiles`):

```sh
bundle exec rake ruby_prototype:benchmark:profile
```

Compare two benchmark JSON files:

```sh
bundle exec ruby ruby-prototype/benchmark/compare_results.rb \
  ruby-prototype/benchmark/results/plain.before.json \
  ruby-prototype/benchmark/results/plain.json \
  --name-before=before --name-after=after
```

Lint:

```sh
bundle exec rake lint
```

Format (C + Ruby):

```sh
bundle exec rake format
```

## License

This project is licensed under the BSD License. See the [LICENSE](LICENSE) file for details.

Some code snippets are derived from Oniguruma, Onigmo and Ruby, which are also BSD-licensed. See the [COPYRIGHT](COPYRIGHT) file for details.
