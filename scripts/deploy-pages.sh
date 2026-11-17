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

# Keep the files of the build before this one. Pages lets browsers and its CDN
# cache every file for 10 minutes, so for a while after a deploy a page loaded
# earlier still asks for the old, hash-named files (the benchmark worker, the
# engine module); deleting them at once makes such a page fail. Each build
# lists its own files in assets/.build, so only one build back is kept.
remote="$(git remote get-url origin)"
if git ls-remote --exit-code --heads "$remote" gh-pages >/dev/null; then
  old="$(mktemp -d)"
  git clone -q --depth 1 --branch gh-pages "$remote" "$old"
  kept=0
  if [ -f "$old/assets/.build" ]; then
    while read -r name; do
      if [ -n "$name" ] && [ -f "$old/assets/$name" ] && [ ! -e "$site/assets/$name" ]; then
        cp "$old/assets/$name" "$site/assets/$name"
        kept=$((kept + 1))
      fi
    done < "$old/assets/.build"
  else
    # The first deploy with this rule: the old branch has no list yet, so
    # keep all of its assets once.
    for f in "$old"/assets/*; do
      name="$(basename "$f")"
      [ -e "$site/assets/$name" ] || { cp "$f" "$site/assets/$name"; kept=$((kept + 1)); }
    done
  fi
  rm -rf "$old"
  echo "Kept $kept files of the previous build for pages still in caches."
fi
(cd web/dist/assets && ls -1) > "$site/assets/.build"
git -C "$site" init -q -b gh-pages
git -C "$site" add -A
git -C "$site" -c user.name="$(git config user.name)" -c user.email="$(git config user.email)" \
  commit -q -m "Build of $source_commit"
if "$push"; then
  git -C "$site" push -q -f "$remote" gh-pages
  echo "Pushed the viewer built from $source_commit to gh-pages."
else
  echo "Dry run: built and committed in $site, not pushed."
  git -C "$site" log --stat --oneline | head -20
fi
