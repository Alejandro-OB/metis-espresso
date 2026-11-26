#!/bin/bash
# Copia de la evidencia de la plataforma (ADR-015 § 4.3).
#
# La decisión de alojar los datos en la tarjeta se sostiene sobre esta copia: una
# tarjeta falla sin avisar, y la protección contra eso no es el medio sino tener
# copia verificada. Por eso el guion termina comprobando lo que escribió, y por
# eso existe `restaurar.sh` a su lado: una copia que nunca se ha restaurado no
# acredita nada.
#
# Uso: respaldar.sh [directorio-destino]
set -euo pipefail

DESTINO="${1:-/mnt/datos/respaldos}"
COMPOSE="${COMPOSE:-$HOME/espresso/platform/web/compose.yaml}"
MARCA="$(date +%Y%m%d-%H%M%S)"
CARPETA="$DESTINO/$MARCA"

docker() { sudo docker "$@"; }

if ! mountpoint -q "$(dirname "$DESTINO")" 2>/dev/null && [ ! -d "$DESTINO" ]; then
  echo "El destino $DESTINO no está disponible: sin copia no hay respaldo." >&2
  exit 1
fi

mkdir -p "$CARPETA"
echo "Copia en $CARPETA"

# El inventario se cuenta hasta un corte tomado antes del volcado, con 10 s de
# margen para las escrituras en curso. Contarlo todo después del volcado, como se
# hacía, sumaba lo que la máquina enviaba mientras se copiaba el grafo: el
# 2026-10-30 la restauración salió exacta y aun así no coincidía por 26 muestras
# en vivo llegadas en esos 30 s. Todo lo creado hasta el corte está en el volcado.
CORTE="$(docker exec aplicacion-web-db-1 psql -U espresso -d espresso -At \
  -c "select to_char((now() - interval '10 seconds') at time zone 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"');")"

# La base de datos, como volcado lógico: sobrevive a cambios de versión del motor,
# que una copia de sus archivos no garantiza.
docker exec aplicacion-web-db-1 pg_dump -U espresso -d espresso --format=custom \
  > "$CARPETA/base-de-datos.dump"

# El grafo y los artefactos, como archivos: son volúmenes opacos para el sistema.
for VOLUMEN in rdf-data simulation-data; do
  docker run --rm \
    -v "aplicacion-web_${VOLUMEN}:/origen:ro" \
    -v "$CARPETA:/destino" \
    debian:trixie-slim \
    tar czf "/destino/${VOLUMEN}.tar.gz" -C /origen . 2>/dev/null \
    || echo "  aviso: el volumen ${VOLUMEN} no se pudo copiar" >&2
done

# Qué había dentro hasta el corte, para poder comprobar la restauración sin adivinar.
docker exec aplicacion-web-db-1 psql -U espresso -d espresso -t -A -F',' \
  -c "select kind, count(*) from documents where created_at <= '$CORTE' group by kind order by 1;" \
  > "$CARPETA/inventario.csv"

( cd "$CARPETA" && sha256sum ./* > sumas.txt )

cat > "$CARPETA/copia.json" <<PROCEDENCIA
{
  "generadaEn": "$(date -Is)",
  "equipo": "$(uname -n)",
  "destino": "$CARPETA",
  "contenido": ["base-de-datos.dump", "rdf-data.tar.gz", "simulation-data.tar.gz"],
  "inventario": "inventario.csv",
  "inventarioHasta": "$CORTE",
  "sumas": "sumas.txt"
}
PROCEDENCIA

echo "Tamaño: $(du -sh "$CARPETA" | cut -f1)"
echo "Documentos respaldados:"
sed 's/^/  /' "$CARPETA/inventario.csv"
