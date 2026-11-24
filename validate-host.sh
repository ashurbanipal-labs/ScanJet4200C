#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${HP4200_BUILD_DIR:-"$project_dir/build-host"}

case ${1:-} in
    '') probe=0 ;;
    --probe) probe=1 ;;
    *) printf 'Usage: %s [--probe]\n' "$0" >&2; exit 2 ;;
esac

printf 'Host: %s %s\n' "$(uname -s)" "$(uname -m)"
cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel 2
ctest --test-dir "$build_dir" --output-on-failure

if [ "$probe" -eq 1 ]; then
    "$build_dir/bin/probe"
fi

printf 'Validation passed. Build: %s\n' "$build_dir"
