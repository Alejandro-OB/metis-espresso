#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(mktemp -d /tmp/eko-fmu-reducida-XXXXXX)"
LOG_FILE="$WORK_DIR/export.log"

cleanup() {
  if [[ -n "${WORK_DIR:-}" && "$WORK_DIR" == /tmp/eko-fmu-reducida-* ]]; then
    rm -rf -- "$WORK_DIR"
  fi
}
trap cleanup EXIT

build_native() {
  command -v omc >/dev/null 2>&1 || return 1
  (
    cd "$WORK_DIR"
    MODELICA_SOURCE_ROOT="$SCRIPT_DIR" omc "$SCRIPT_DIR/export_fmu.mos"
  ) >"$LOG_FILE" 2>&1 || return 1
  [[ -f "$WORK_DIR/EkoCoffeeMachine.fmu" ]]
}

build_container() {
  command -v docker >/dev/null 2>&1 || return 1
  docker image inspect openmodelica-mcp:local >/dev/null 2>&1 || return 1
  docker run --rm --entrypoint sh \
    -v "$SCRIPT_DIR:/src:ro" \
    -v "$WORK_DIR:/work" \
    -w /work \
    openmodelica-mcp:local \
    -lc 'dnf install -y zip >/dev/null && MODELICA_SOURCE_ROOT=/src omc /src/export_fmu.mos; status=$?; chmod -R a+rwX /work; exit $status' \
    >"$LOG_FILE" 2>&1
  [[ -f "$WORK_DIR/EkoCoffeeMachine.fmu" ]]
}

if ! build_native; then
  echo "La exportación nativa no fue posible; se intentará el entorno OpenModelica aislado." >&2
  if ! build_container; then
    echo "No se pudo construir la FMU. Registro:" >&2
    sed -n '1,160p' "$LOG_FILE" >&2
    exit 1
  fi
fi

mkdir -p "$SCRIPT_DIR/dist"
install -m 0644 "$WORK_DIR/EkoCoffeeMachine.fmu" "$SCRIPT_DIR/dist/EkoCoffeeMachine.fmu"
echo "FMU exportada en $SCRIPT_DIR/dist/EkoCoffeeMachine.fmu"
