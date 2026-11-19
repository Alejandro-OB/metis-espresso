#!/bin/bash
# Recompila el artefacto del modelo para la arquitectura donde se ejecuta,
# a partir de las fuentes que el propio artefacto lleva dentro (ADR-011 § 4.1).
#
# No reexporta el modelo: eso exigiría la herramienta de modelado en el equipo de
# operación, y ADR-011 § 3.1 lo descartó explícitamente. Aquí solo se compila C.
#
# Uso, dentro del contenedor:
#   recompilar-artefacto <fmu-entrada> <directorio-salida>
#
# Escribe en el directorio de salida:
#   <nombre>.fmu             el artefacto recompilado
#   <nombre>.procedencia.json  con qué se compiló, que es parte de la evidencia
#
# El artefacto de salida NO sustituye al de entrada: son dos artefactos del mismo
# modelo, uno por arquitectura, tal como decide ADR-011 § 4.2. En FMI 2.0 ambos
# reclaman la misma carpeta `binaries/linux64`, de modo que no pueden convivir en
# un mismo fichero.
set -euo pipefail

ENTRADA="${1:?falta la ruta del artefacto de entrada}"
SALIDA="${2:?falta el directorio de salida}"

NOMBRE="$(basename "${ENTRADA%.fmu}")"
TRABAJO="$(mktemp -d)"
trap 'rm -rf "$TRABAJO"' EXIT

sha() { sha256sum "$1" | cut -d" " -f1; }

SHA_ENTRADA="$(sha "$ENTRADA")"
ARQUITECTURA="$(uname -m)"

echo "Recompilando ${NOMBRE} para ${ARQUITECTURA}"
mkdir -p "$TRABAJO/fmu"
unzip -q "$ENTRADA" -d "$TRABAJO/fmu"

if [ ! -d "$TRABAJO/fmu/sources" ]; then
  echo "El artefacto no incluye sus fuentes: no puede recompilarse aquí." >&2
  exit 1
fi

# El propio CMakeLists instala la biblioteca en ../binaries/<plataforma> respecto
# de las fuentes, es decir, dentro del artefacto extraído. No hay que moverla.
# CVODE_DIRECTORY apunta a la SUNDIALS de la imagen; el CMakeLists del artefacto
# solo lo usa si el modelo integra con CVODE (NEED_CVODE), y si no, lo ignora.
# SUNDIALS_PREFIX permite usar el mismo guion sobre el sistema cuando el motor de
# contenedores no tiene red, como pasó en la plataforma el 2026-09-15 y el
# 2026-10-19 (ADR-011, entorno de construcción): la SUNDIALS se compila entonces con los mismos
# argumentos que el Dockerfile, en un prefijo del usuario.
SUNDIALS_PREFIX="${SUNDIALS_PREFIX:-/opt/sundials}"
cmake -S "$TRABAJO/fmu/sources" -B "$TRABAJO/build" \
      -DCMAKE_BUILD_TYPE=Release -DCVODE_DIRECTORY="$SUNDIALS_PREFIX/lib" \
      > "$TRABAJO/configuracion.log" 2>&1
cmake --build "$TRABAJO/build" --parallel "$(nproc)" > "$TRABAJO/compilacion.log" 2>&1
cmake --install "$TRABAJO/build" > "$TRABAJO/instalacion.log" 2>&1

BIBLIOTECA="$(find "$TRABAJO/fmu/binaries" -name "*.so" -newer "$ENTRADA" | head -1)"
if [ -z "$BIBLIOTECA" ]; then
  echo "La compilación no produjo ninguna biblioteca nueva." >&2
  tail -20 "$TRABAJO/compilacion.log" >&2
  exit 1
fi

# La arquitectura del binario se lee del propio fichero, no se supone: es lo que
# distingue este artefacto del original y lo que hay que poder demostrar.
ARQ_BINARIO="$(head -c 19 "$BIBLIOTECA" | od -An -t u1 -j 18 -N 1 | tr -d ' ')"
case "$ARQ_BINARIO" in
  183) ARQ_LEIDA="aarch64" ;;
  62)  ARQ_LEIDA="x86_64" ;;
  *)   ARQ_LEIDA="desconocida(${ARQ_BINARIO})" ;;
esac

mkdir -p "$SALIDA"
DESTINO="$SALIDA/${NOMBRE}-${ARQUITECTURA}.fmu"
rm -f "$DESTINO"
( cd "$TRABAJO/fmu" && zip -q -r "$DESTINO" . )

cat > "$SALIDA/${NOMBRE}-${ARQUITECTURA}.procedencia.json" <<PROCEDENCIA
{
  "generadoEn": "$(date -Is)",
  "arquitecturaDeCompilacion": "${ARQUITECTURA}",
  "arquitecturaLeidaDelBinario": "${ARQ_LEIDA}",
  "sistema": "$(uname -sr)",
  "compilador": "$(cc --version | head -1)",
  "cmake": "$(cmake --version | head -1)",
  "opciones": "CMAKE_BUILD_TYPE=Release CVODE_DIRECTORY=${SUNDIALS_PREFIX}/lib",
  "sundials": "$(grep -m1 SUNDIALS_VERSION ${SUNDIALS_PREFIX}/include/sundials/sundials_config.h 2>/dev/null | cut -d'"' -f2)",
  "artefactoOrigen": {
    "nombre": "$(basename "$ENTRADA")",
    "sha256": "${SHA_ENTRADA}"
  },
  "artefactoGenerado": {
    "nombre": "$(basename "$DESTINO")",
    "sha256": "$(sha "$DESTINO")",
    "bibliotecaSha256": "$(sha "$BIBLIOTECA")"
  }
}
PROCEDENCIA

echo "Artefacto:   $DESTINO"
echo "Arquitectura del binario: ${ARQ_LEIDA}"
echo "Procedencia: $SALIDA/${NOMBRE}-${ARQUITECTURA}.procedencia.json"
