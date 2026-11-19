#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"
main_dir="$project_root/firmware/esp32-s3-single-mcu/main"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$main_dir" \
  "$main_dir/sensor_scaling.c" \
  "$main_dir/signal_quality.c" \
  "$main_dir/scale_reading.c" \
  "$project_root/tests/host/test_scale_reading.c" \
  -lm -o "$output_dir/test_scale_reading"

"$output_dir/test_scale_reading"
