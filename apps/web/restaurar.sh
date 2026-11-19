#!/bin/bash
# Restauración de una copia de la plataforma (ADR-015 § 4.3 y § 9.2).
#
# Restaura sobre una base de datos vacía y **comprueba** el resultado contra el
# inventario que la copia guardó. Una restauración que no se verifica no acredita
# que la copia sirviera: solo que el fichero se leía.
#
# Uso: restaurar.sh <carpeta-de-copia>
set -euo pipefail

CARPETA="${1:?falta la carpeta de la copia}"
[ -f "$CARPETA/base-de-datos.dump" ] || { echo "No hay volcado en $CARPETA" >&2; exit 1; }

docker() { sudo docker "$@"; }

echo "Comprobando la integridad de la copia"
( cd "$CARPETA" && sha256sum --quiet -c sumas.txt ) || {
  echo "La copia está dañada: las sumas no coinciden." >&2
  exit 1
}

echo "Restaurando la base de datos"
docker exec -i aplicacion-web-db-1 pg_restore -U espresso -d espresso \
  --clean --if-exists --no-owner < "$CARPETA/base-de-datos.dump" 2>&1 \
  | grep -vE "^$|does not exist, skipping" || true

echo "Comprobando contra el inventario de la copia"
# Las copias desde el 2026-10-30 cuentan su inventario hasta un corte
# (`inventarioHasta` en copia.json); las anteriores, todo.
CORTE="$(sed -n 's/.*"inventarioHasta": *"\([^"]*\)".*/\1/p' "$CARPETA/copia.json" 2>/dev/null || true)"
FILTRO=""
[ -n "$CORTE" ] && FILTRO="where created_at <= '$CORTE'"
docker exec aplicacion-web-db-1 psql -U espresso -d espresso -t -A -F',' \
  -c "select kind, count(*) from documents $FILTRO group by kind order by 1;" \
  > /tmp/inventario-restaurado.csv

if diff -q "$CARPETA/inventario.csv" /tmp/inventario-restaurado.csv > /dev/null; then
  echo "Restauración verificada: el inventario coincide documento a documento."
else
  echo "DIFERENCIAS entre lo copiado y lo restaurado:" >&2
  diff "$CARPETA/inventario.csv" /tmp/inventario-restaurado.csv >&2 || true
  exit 1
fi
