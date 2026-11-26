#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$project_root/main" \
  "$project_root/main/operational_fsm.c" \
  "$project_root/main/simulated_plant.c" \
  "$project_root/main/process_supervisor.c" \
  "$project_root/main/extraction_profile.c" \
  "$project_root/main/extraction_session.c" \
  "$project_root/main/pre_start_history.c" \
  "$project_root/tests/host/test_extraction_execution.c" \
  -lm -o "$output_dir/test_extraction_execution"

"$output_dir/test_extraction_execution"
