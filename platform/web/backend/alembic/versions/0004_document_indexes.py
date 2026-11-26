"""Índices para buscar documentos por campo del contenido y por fecha.

La tabla `documents` solo tenía su clave (tipo, identificador). Con cientos de
miles de muestras de telemetría, cada búsqueda de la serie de una preparación,
de la última telemetría de estado o de las muestras de un intervalo recorría la
tabla entera y leía cada JSON. Con el panel de la máquina preguntando cada pocos
segundos las consultas llegaron a tardar un minuto, se agotaron las conexiones
y la sincronización de la placa quedó esperando: el panel mostraba «Sin señal»
con la placa enviando (19-oct-2026).

Los índices son de expresión sobre `payload ->> 'campo'`: el contenido es JSON
sin esquema y no se quiere una columna por campo. El adaptador pasa la clave
como parámetro y PostgreSQL la sustituye al planificar, así que el índice se
usa (comprobado con EXPLAIN sobre la consulta preparada).

Solo PostgreSQL: SQLite, que usan las pruebas, no los necesita.

Revision ID: 0004
"""

from alembic import op

revision = "0004"
down_revision = "0003"
branch_labels = None
depends_on = None

INDEXES = {
    "ix_documents_kind_created": "documents (kind, created_at DESC)",
    "ix_documents_kind_extraction": "documents (kind, (payload ->> 'extractionId'))",
    "ix_documents_kind_device_instant": (
        "documents (kind, (payload ->> 'deviceId'), (payload ->> 'occurredAtUtc'))"
    ),
    # La última telemetría con estado de la máquina: las muestras de las series
    # no lo llevan y son la mayoría.
    "ix_documents_machine_telemetry": (
        "documents (created_at DESC) "
        "WHERE kind = 'live-sample' AND (payload ->> 'state') IS NOT NULL"
    ),
}


def upgrade() -> None:
    if op.get_bind().dialect.name != "postgresql":
        return
    for name, definition in INDEXES.items():
        op.execute(f"CREATE INDEX IF NOT EXISTS {name} ON {definition}")


def downgrade() -> None:
    if op.get_bind().dialect.name != "postgresql":
        return
    for name in INDEXES:
        op.execute(f"DROP INDEX IF EXISTS {name}")
