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
  "$project_root/tests/host/test_extraction_message.c" \
  -lm -o "$output_dir/test_extraction_message"

sample="$output_dir/extraction-message-sample.json"
unscored="$output_dir/extraction-message-never-stabilized.json"
power_loss="$output_dir/extraction-message-power-loss.json"
power_loss_undated="$output_dir/extraction-message-power-loss-undated.json"
compensated="$output_dir/extraction-message-first-use-offset.json"
"$output_dir/test_extraction_message" "$sample" "$unscored" "$power_loss" "$power_loss_undated" \
  "$compensated"

# El mensaje generado por el firmware debe validar contra el contrato publicado,
# también cuando los indicadores van en null y cuando un corte de energía deja
# sin fin ni duración la preparación, y con la compensación del primer uso.
for message in "$sample" "$unscored" "$power_loss" "$power_loss_undated" "$compensated"; do
  python3 "$project_root/tests/host/validate_envelope.py" "$message" \
    "$repository_root/device/software/contracts/v1"
done
