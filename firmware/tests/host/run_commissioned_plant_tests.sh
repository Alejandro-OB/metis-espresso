#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir="$project_root/tests/build"
main_dir="$project_root/main"
mkdir -p "$output_dir"

sources="$main_dir/operational_fsm.c $main_dir/process_supervisor.c \
  $main_dir/control_plant.c $main_dir/control_tuning.c \
  $project_root/tests/host/test_commissioned_plant.c"

# Primera compilación: el repositorio tal como está, sin planta identificada.
cc -std=c17 -Wall -Wextra -Werror -pedantic -I"$main_dir" $sources \
  -lm -o "$output_dir/test_commissioned_plant"
"$output_dir/test_commissioned_plant"
