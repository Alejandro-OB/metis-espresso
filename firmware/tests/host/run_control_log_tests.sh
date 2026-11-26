#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$project_root/main" \
  "$project_root/main/operational_fsm.c" \
  "$project_root/main/actuation_guard.c" \
  "$project_root/main/sensor_scaling.c" \
  "$project_root/main/signal_quality.c" \
  "$project_root/main/stimulus_sequence.c" \
  "$project_root/main/control_log.c" \
  "$project_root/tests/host/test_control_log.c" \
  -lm -o "$output_dir/test_control_log"

"$output_dir/test_control_log"
