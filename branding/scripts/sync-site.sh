#!/bin/sh
# Copies the brand assets the website uses into site/assets/brand/ (plus site/favicon.ico).
# Run after `npm run export` whenever the logo changes.
set -eu
b=$(cd "$(dirname "$0")/.." && pwd)
site="$b/../site"
out="$site/assets/brand"
rm -rf "$out"
mkdir -p "$out"
for f in icon icon-small mark mark-small; do
  cp "$b/svg/$f.svg" "$out/"
done
cp "$b/exports/web/favicon.ico" "$b/exports/web/favicon.svg" "$b/exports/web/apple-touch-icon.png" \
   "$b/exports/web/icon-192.png" "$b/exports/web/icon-512.png" "$out/"
cp "$b/exports/web/favicon.ico" "$site/favicon.ico"
echo "synced $(ls "$out" | wc -l | tr -d ' ') files to site/assets/brand/"
