#!/usr/bin/env bash
# Optional local Arduino ctags build for Apple Silicon without Rosetta.
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CTAGS_DIR="$PROJECT_DIR/.cache/arduino-ctags"
mkdir -p "$PROJECT_DIR/.cache"
if [[ ! -d "$CTAGS_DIR/.git" ]]; then
  git clone --depth 1 --branch 5.8-arduino11 https://github.com/arduino/ctags.git "$CTAGS_DIR"
fi
cd "$CTAGS_DIR"
if [[ "$(git rev-parse HEAD)" != abc8fca7499f44c725122881cd380a88c37abe0e ]]; then
  echo 'Unexpected Arduino ctags revision; refusing to patch.' >&2
  exit 1
fi
# These old internal macro names collide with modern macOS SDK attributes.
perl -pi -e 's/\b__unused__\b/CTAGS_UNUSED/g; s/\b__printf__\b/CTAGS_PRINTF/g' ./*.c ./*.h
./configure
make -j4
./ctags --version
