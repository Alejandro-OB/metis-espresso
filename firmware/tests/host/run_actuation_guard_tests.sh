#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$project_root/main" \
  "$project_root/main/operational_fsm.c" \
  "$project_root/main/actuation_guard.c" \
  "$project_root/tests/host/test_actuation_guard.c" \
  -o "$output_dir/test_actuation_guard"

"$output_dir/test_actuation_guard"
