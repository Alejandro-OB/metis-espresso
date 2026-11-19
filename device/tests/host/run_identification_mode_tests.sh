#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$project_root/firmware/esp32-s3-single-mcu/main" \
  "$project_root/firmware/esp32-s3-single-mcu/main/operational_fsm.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/actuation_guard.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/process_supervisor.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/stimulus_sequence.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/identification_mode.c" \
  "$project_root/tests/host/test_identification_mode.c" \
  -o "$output_dir/test_identification_mode"

"$output_dir/test_identification_mode"
