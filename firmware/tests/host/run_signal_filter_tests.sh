#!/usr/bin/env sh
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"
cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$project_root/main" \
  "$project_root/main/signal_filter.c" \
  "$project_root/tests/host/test_signal_filter.c" \
  -lm -o "$output_dir/test_signal_filter"
"$output_dir/test_signal_filter"
