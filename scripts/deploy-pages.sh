#!/usr/bin/env bash
# Publishes the web viewer to GitHub Pages: builds the WebAssembly module and
# the page, then commits web/dist as the only content of the gh-pages branch
# and pushes it. GitHub Actions is off for this repo, so Pages serves the
# branch as it is ("Deploy from a branch" in the repo settings).
#
# Usage: scripts/deploy-pages.sh            build and push
#        scripts/deploy-pages.sh --dry-run  build and commit, do not push
#
# Run it from a clean main after merging; the commit message names the main
# commit the page was built from.
set -euo pipefail

cd "$(dirname "$0")/.."

push=true
[ "${1:-}" = "--dry-run" ] && push=false

if ! command -v emcc >/dev/null; then
  # shellcheck disable=SC1091
  source "${EMSDK:-$HOME/emsdk}/emsdk_env.sh" >/dev/null 2>&1
fi

source_commit="$(git rev-parse --short HEAD)"
if [ -n "$(git status --porcelain)" ]; then
  echo "the working tree has changes; deploy from a clean checkout" >&2
  exit 1
fi

cmake --preset wasm
cmake --build --preset wasm -j"$(nproc)"
(cd web && npm ci && npm run build)
# Without it Pages runs Jekyll, which skips files starting with "_".
touch web/dist/.nojekyll

# A throwaway repo whose single commit is the built site, force-pushed over
# gh-pages: the branch holds no history of old builds.
site="$(mktemp -d)"
trap 'rm -rf "$site"' EXIT
cp -r web/dist/. "$site"
git -C "$site" init -q -b gh-pages
git -C "$site" add -A
git -C "$site" -c user.name="$(git config user.name)" -c user.email="$(git config user.email)" \
  commit -q -m "Build of $source_commit"
if "$push"; then
  git -C "$site" push -q -f "$(git remote get-url origin)" gh-pages
  echo "Pushed the viewer built from $source_commit to gh-pages."
else
  echo "Dry run: built and committed in $site, not pushed."
  git -C "$site" log --stat --oneline | head -20
fi
