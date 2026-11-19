#!/usr/bin/env sh
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
main_dir="$project_root/firmware/esp32-s3-single-mcu/main"
mkdir -p "$output_dir"
cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$main_dir" \
  "$main_dir/first_use_offset.c" \
  "$main_dir/operational_fsm.c" \
  "$main_dir/process_supervisor.c" \
  "$project_root/tests/host/test_first_use_offset.c" \
  -lm -o "$output_dir/test_first_use_offset"
"$output_dir/test_first_use_offset"
