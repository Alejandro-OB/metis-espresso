#!/bin/bash
set -e

# Fuseki exige un shiro.ini en FUSEKI_BASE. Se copia el del paquete oficial la
# primera vez y se fija la contraseña de administración declarada.
if [ ! -f "${FUSEKI_BASE}/shiro.ini" ]; then
  cp "${FUSEKI_HOME}/shiro.ini" "${FUSEKI_BASE}/shiro.ini"
fi

if [ -n "${ADMIN_PASSWORD}" ]; then
  sed -i "s/^admin=.*/admin=${ADMIN_PASSWORD}/" "${FUSEKI_BASE}/shiro.ini"
fi

exec "$@"
