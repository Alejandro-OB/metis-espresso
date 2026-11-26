#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
repository_root=$(CDPATH= cd -- "$project_root/.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$project_root/main" \
  "$project_root/main/extraction_profile.c" \
  "$project_root/main/extraction_session.c" \
  "$project_root/main/pre_start_history.c" \
  "$project_root/main/extraction_message.c" \
  "$project_root/main/water_reference.c" \
  "$project_root/main/series_message.c" \
  "$project_root/tests/host/test_series_message.c" \
  -lm -o "$output_dir/test_series_message"

sample="$output_dir/series-message-sample.json"
"$output_dir/test_series_message" "$sample"

# El tramo generado por el firmware debe validar contra el contrato publicado.
python3 "$project_root/tests/host/validate_envelope.py" "$sample" \
  "$repository_root/platform/contracts/device"
