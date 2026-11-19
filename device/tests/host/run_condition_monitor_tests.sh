#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
repository_root=$(CDPATH= cd -- "$project_root/.." && pwd)
output_dir="$project_root/tests/build"
mkdir -p "$output_dir"

cc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$project_root/firmware/esp32-s3-single-mcu/main" \
  "$project_root/firmware/esp32-s3-single-mcu/main/extraction_profile.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/extraction_session.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/pre_start_history.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/extraction_message.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/water_reference.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/operational_fsm.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/actuation_guard.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/condition_monitor.c" \
  "$project_root/firmware/esp32-s3-single-mcu/main/event_message.c" \
  "$project_root/tests/host/test_condition_monitor.c" \
  -lm -o "$output_dir/test_condition_monitor"

sample="$output_dir/condition-event-sample.json"
"$output_dir/test_condition_monitor" "$sample"

# El aviso generado debe validar contra el contrato publicado.
python3 "$project_root/tests/host/validate_envelope.py" "$sample" \
  "$repository_root/device/software/contracts/v1"
