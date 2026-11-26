#!/bin/sh
set -eu
temporary_dir=$(mktemp -d)
server_pid=''
cleanup() {
  test -z "$server_pid" || kill "$server_pid" 2>/dev/null || true
  rm -rf "$temporary_dir"
}
trap cleanup EXIT INT TERM
APP_ENVIRONMENT=test \
APP_DATABASE_URL="sqlite+pysqlite:///$temporary_dir/openapi.sqlite3" \
APP_SEED_DEMO=false \
PYTHONPATH=backend/src \
backend/.venv/bin/uvicorn aplicacion_web.main:app --host 127.0.0.1 --port 18761 >"$temporary_dir/server.log" 2>&1 &
server_pid=$!
attempt=0
until curl --fail --silent http://127.0.0.1:18761/health/ready >/dev/null; do
  attempt=$((attempt + 1))
  test "$attempt" -lt 30 || { cat "$temporary_dir/server.log"; exit 1; }
  sleep 0.2
done
cd frontend
npx openapi-typescript http://127.0.0.1:18761/api/openapi.json -o "$temporary_dir/schema.d.ts"
diff -u src/shared/api/schema.d.ts "$temporary_dir/schema.d.ts"
