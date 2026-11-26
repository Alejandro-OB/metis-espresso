#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$project_root/main" \
  "$project_root/main/control_tuning.c" \
  "$project_root/main/control_plant.c" \
  "$project_root/main/identification_tuning.c" \
  "$project_root/tests/host/test_identification_tuning.c" \
  -lm -o "$output_dir/test_identification_tuning"

"$output_dir/test_identification_tuning"
