#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
main_dir="$project_root/firmware/esp32-s3-single-mcu/main"
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$main_dir" \
  "$main_dir/hydraulic_test.c" \
  "$project_root/tests/host/test_hydraulic_test.c" \
  -lm -o "$output_dir/test_hydraulic_test"

"$output_dir/test_hydraulic_test"
