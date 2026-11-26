#!/usr/bin/env sh
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"
cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$project_root/main" \
  "$project_root/main/pressure_preinfusion.c" \
  "$project_root/main/pi_controller.c" \
  "$project_root/tests/host/test_pressure_preinfusion.c" \
  -lm -o "$output_dir/test_pressure_preinfusion"
"$output_dir/test_pressure_preinfusion"
