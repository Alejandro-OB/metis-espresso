#!/usr/bin/env sh
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"
cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$project_root/main" \
  "$project_root/main/heater_fast_start.c" \
  "$project_root/tests/host/test_heater_fast_start.c" \
  -lm -o "$output_dir/test_heater_fast_start"
"$output_dir/test_heater_fast_start"
