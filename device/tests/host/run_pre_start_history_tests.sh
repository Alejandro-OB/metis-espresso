#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$project_root/firmware/esp32-s3-single-mcu/main" \
  "$project_root/firmware/esp32-s3-single-mcu/main/pre_start_history.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/extraction_session.c" \
  "$project_root/tests/host/test_pre_start_history.c" \
  -lm -o "$output_dir/test_pre_start_history"

"$output_dir/test_pre_start_history"
