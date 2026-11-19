#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$project_root/firmware/esp32-s3-single-mcu/main" \
  "$project_root/firmware/esp32-s3-single-mcu/main/operational_fsm.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/simulated_plant.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/process_supervisor.c" \
  "$project_root/tests/host/test_simulated_plant.c" \
  -o "$output_dir/test_simulated_plant"

"$output_dir/test_simulated_plant"
