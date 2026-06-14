#!/usr/bin/env bash
set -euo pipefail

WORKSPACE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# --- System dependencies ---
sudo apt-get update -qq
sudo apt-get install -y \
  build-essential bison clang clang-format gperf \
  libssl-dev libreadline-dev zlib1g-dev \
  libffi-dev libyaml-dev libgmp-dev \
  lcov

# --- rbenv + Ruby ---
if [ ! -d "$HOME/.rbenv/.git" ]; then
  git clone https://github.com/rbenv/rbenv.git ~/.rbenv
else
  git -C "$HOME/.rbenv" pull
fi
if [ ! -d "$HOME/.rbenv/plugins/ruby-build" ]; then
  git clone https://github.com/rbenv/ruby-build.git ~/.rbenv/plugins/ruby-build
else
  git -C "$HOME/.rbenv/plugins/ruby-build" pull
fi

export PATH="$HOME/.rbenv/bin:$PATH"
eval "$(rbenv init -)"

# --- rustup + rustc (required for YJIT-enabled Ruby) ---
if ! command -v rustc &>/dev/null; then
  curl -fsSL https://sh.rustup.rs | sh -s -- -y --no-modify-path
fi
export PATH="$HOME/.cargo/bin:$PATH"

RUBY_VERSION=$(tr -d '[:space:]' < "$WORKSPACE_ROOT/.ruby-version")
if ! rbenv versions --bare | grep -qx "$RUBY_VERSION"; then
  RUBY_CONFIGURE_OPTS="--enable-yjit" rbenv install "$RUBY_VERSION"
fi
rbenv global "$RUBY_VERSION"

# --- mruby submodule ---
git -C "$WORKSPACE_ROOT" submodule update --init

# --- Ruby gems ---
cd "$WORKSPACE_ROOT"
gem install bundler --no-document
bundle install

# --- Download Unicode Character Database ---
ruby tools/download_ucd.rb 17.0.0

# --- Build mruby ---
bundle exec rake naraku:build_mruby

# --- gh CLI ---
if ! command -v gh &>/dev/null; then
  curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
    | sudo dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg 2>/dev/null
  sudo chmod go+r /usr/share/keyrings/githubcli-archive-keyring.gpg
  echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
    | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null
  sudo apt-get update -qq
  sudo apt-get install -y gh
fi

# --- gitleaks (secret scanner used in pre-commit hook) ---
GITLEAKS_VERSION="8.30.1"
if ! command -v gitleaks &>/dev/null; then
  curl -fsSL "https://github.com/gitleaks/gitleaks/releases/download/v${GITLEAKS_VERSION}/gitleaks_${GITLEAKS_VERSION}_linux_x64.tar.gz" \
    | sudo tar -xz -C /usr/local/bin gitleaks
fi

# --- Pre-commit hook ---
cp "$WORKSPACE_ROOT/.hooks/pre-commit" "$WORKSPACE_ROOT/.git/hooks/pre-commit"
chmod +x "$WORKSPACE_ROOT/.git/hooks/pre-commit"

# --- Shell profile ---
PROFILE="$HOME/.bashrc"
add_if_missing() {
  grep -qF "$1" "$PROFILE" || echo "$1" >> "$PROFILE"
}

add_if_missing 'export PATH="$HOME/.cargo/bin:$HOME/.rbenv/bin:$PATH"'
add_if_missing 'eval "$(rbenv init -)"'

# --- Claude Code (optional) ---
# To install Claude Code (https://claude.ai/code), create a marker file at the
# workspace root (gitignored, so it's local-only and scoped to this clone):
#   touch .install-claude-code
if [ -f "$WORKSPACE_ROOT/.install-claude-code" ] && ! command -v claude &>/dev/null; then
  curl -fsSL https://claude.ai/install.sh | bash
fi
