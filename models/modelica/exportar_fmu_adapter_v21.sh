#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(mktemp -d /tmp/eko-fmu-adapter-v21-XXXXXX)"
LOG_FILE="$WORK_DIR/export.log"

cleanup() {
  if [[ -n "${WORK_DIR:-}" && "$WORK_DIR" == /tmp/eko-fmu-adapter-v21-* ]]; then
    rm -rf -- "$WORK_DIR"
  fi
}
trap cleanup EXIT

build_native() {
  command -v omc >/dev/null 2>&1 || return 1
  (
    cd "$WORK_DIR"
    MODELICA_PROJECT_ROOT="$PROJECT_ROOT" omc "$PROJECT_ROOT/exportar_fmu_adapter_v21.mos"
  ) >"$LOG_FILE" 2>&1 || return 1
  [[ -f "$WORK_DIR/FmuAdapterV21.fmu" ]]
}

build_container() {
  command -v docker >/dev/null 2>&1 || return 1
  docker image inspect openmodelica-mcp:local >/dev/null 2>&1 || return 1
  docker run --rm --entrypoint sh \
    -v "$PROJECT_ROOT:/project:ro" \
    -v "$WORK_DIR:/work" \
    -v "${HOME}/.openmodelica/libraries:/root/.openmodelica/libraries:ro" \
    -w /work \
    openmodelica-mcp:local \
    -lc 'dnf install -y zip >/dev/null && MODELICA_PROJECT_ROOT=/project omc /project/exportar_fmu_adapter_v21.mos; status=$?; chmod -R a+rwX /work; exit $status' \
    >"$LOG_FILE" 2>&1
  [[ -f "$WORK_DIR/FmuAdapterV21.fmu" ]]
}

if ! build_native; then
  echo "La exportación nativa no fue posible; se intentará el entorno OpenModelica aislado." >&2
  if ! build_container; then
    echo "No se pudo construir la FMU. Registro:" >&2
    sed -n '1,160p' "$LOG_FILE" >&2
    exit 1
  fi
fi

mkdir -p "$PROJECT_ROOT/dist"
install -m 0644 "$WORK_DIR/FmuAdapterV21.fmu" "$PROJECT_ROOT/dist/FmuAdapterV21.fmu"
echo "FMU exportada: $PROJECT_ROOT/dist/FmuAdapterV21.fmu"
