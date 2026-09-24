#!/usr/bin/env bash
# Build and run heaptop's host unit tests (pure modules only) with the host gcc.
# Unity comes from ESP-IDF ($IDF_PATH) or, failing that, $UNITY_DIR.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
unity=${UNITY_DIR:-${IDF_PATH:?set IDF_PATH or UNITY_DIR}/components/unity/unity/src}
out="$here/build"
mkdir -p "$out"

CC=${CC:-gcc}
CFLAGS="-std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -I$root/include -I$root/src -I$unity"

fail=0
run() {
  local name=$1
  shift
  $CC $CFLAGS "$here/$name.c" "$@" "$unity/unity.c" -o "$out/$name"
  if ! "$out/$name"; then fail=1; fi
}

run test_calc "$root/src/heaptop_calc.c"
run test_render "$root/src/heaptop_render.c" "$root/src/heaptop_calc.c"

exit $fail
