#!/usr/bin/env bash
# scripts/prepare-aur.sh
# Copy packaging files to the project root so makepkg can find them,
# then optionally build the source tarball.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

echo "==> Linking packaging files to project root …"
ln -sf packaging/PKGBUILD       "$ROOT/PKGBUILD"
ln -sf packaging/t9numpad.install "$ROOT/t9numpad.install"

echo "==> Done.  Run 'makepkg -si' in the project root to build and install."
