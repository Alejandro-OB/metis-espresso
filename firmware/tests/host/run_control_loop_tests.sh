#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"
main_dir="$project_root/main"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$main_dir" \
  "$main_dir/control_filter.c" \
  "$main_dir/pi_controller.c" \
  "$main_dir/pressure_preinfusion.c" \
  "$main_dir/heater_recovery_hold.c" \
  "$main_dir/heater_recovery_return.c" \
  "$main_dir/psm_scheduler.c" \
  "$main_dir/heater_modulator.c" \
  "$main_dir/control_plant.c" \
  "$main_dir/control_tuning.c" \
  "$project_root/tests/host/test_control_loop.c" \
  -lm -o "$output_dir/test_control_loop"

"$output_dir/test_control_loop"
