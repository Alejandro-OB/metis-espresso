#!/usr/bin/env bash
set -euo pipefail

readonly REQUIRED_IDF_VERSION="v6.1"
readonly DEFAULT_IDF_PATH="${HOME}/esp/esp-idf-v6.1"
readonly PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SELECTED_IDF_PATH="${IDF_PATH:-$DEFAULT_IDF_PATH}"

if [[ ! -f "$SELECTED_IDF_PATH/export.sh" ]]; then
    printf 'No se encontró ESP-IDF en %s\n' "$SELECTED_IDF_PATH" >&2
    printf 'Instale ESP-IDF %s o defina IDF_PATH.\n' "$REQUIRED_IDF_VERSION" >&2
    exit 1
fi

source "$SELECTED_IDF_PATH/export.sh" >/dev/null

if [[ "$(idf.py --version)" != *"6.1"* ]]; then
    printf 'Advertencia: el proyecto fue validado con ESP-IDF %s.\n' "$REQUIRED_IDF_VERSION" >&2
fi

cd "$PROJECT_DIR"
exec idf.py "$@"
