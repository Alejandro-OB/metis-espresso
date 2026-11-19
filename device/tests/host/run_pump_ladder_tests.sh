#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
main_dir="$project_root/firmware/esp32-s3-single-mcu/main"
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$main_dir" \
  "$main_dir/operational_fsm.c" \
  "$main_dir/actuation_guard.c" \
  "$main_dir/process_supervisor.c" \
  "$main_dir/stimulus_sequence.c" \
  "$main_dir/identification_mode.c" \
  "$main_dir/pump_ladder.c" \
  "$project_root/tests/host/test_pump_ladder.c" \
  -lm -o "$output_dir/test_pump_ladder"

"$output_dir/test_pump_ladder"
