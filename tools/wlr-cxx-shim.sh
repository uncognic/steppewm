#!/bin/sh
set -eu

module=$1
outdir=$2

src=""
for dir in $(pkg-config --cflags-only-I "$module" | tr ' ' '\n' | sed 's/^-I//'); do
	if [ -d "$dir/wlr" ]; then
		src=$dir
		break
	fi
done

if [ -z "$src" ]; then
	echo "wlr-cxx-shim: no wlr/ header tree found in the include flags for $module" >&2
	exit 1
fi

rm -rf "$outdir"
mkdir -p "$outdir"
cp -r "$src/wlr" "$outdir/"
find "$outdir" -name '*.h' -exec sed -i -E 's/\[static [0-9]+\]/[]/g' {} +

echo "$src"
