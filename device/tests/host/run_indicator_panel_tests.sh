#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"
main_dir="$project_root/firmware/esp32-s3-single-mcu/main"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$main_dir" \
  "$main_dir/operational_fsm.c" \
  "$main_dir/actuation_guard.c" \
  "$main_dir/indicator_panel.c" \
  "$project_root/tests/host/test_indicator_panel.c" \
  -o "$output_dir/test_indicator_panel"

"$output_dir/test_indicator_panel"
