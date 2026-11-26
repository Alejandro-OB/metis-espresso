"""Trabajos y artefactos de simulación.

Revision ID: 0003
"""

import sqlalchemy as sa

from alembic import op

revision = "0003"
down_revision = "0002"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "simulation_runs",
        sa.Column("id", sa.String(length=80), nullable=False),
        sa.Column("requested_by", sa.String(length=120), nullable=False),
        sa.Column("idempotency_key", sa.String(length=120), nullable=False),
        sa.Column("request_hash", sa.String(length=64), nullable=False),
        sa.Column("status", sa.String(length=20), nullable=False),
        sa.Column("semantic_status", sa.String(length=20), nullable=False),
        sa.Column("payload", sa.JSON(), nullable=False),
        sa.Column("cancellation_requested", sa.Boolean(), nullable=False),
        sa.Column("worker_id", sa.String(length=120), nullable=True),
        sa.Column("lease_expires_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("heartbeat_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("requested_by", "idempotency_key", name="uq_simulation_idempotency"),
    )
    op.create_index("ix_simulation_runs_requested_by", "simulation_runs", ["requested_by"])
    op.create_index("ix_simulation_runs_status", "simulation_runs", ["status"])
    op.create_index("ix_simulation_runs_semantic_status", "simulation_runs", ["semantic_status"])
    op.create_table(
        "simulation_artifacts",
        sa.Column("id", sa.String(length=100), nullable=False),
        sa.Column("simulation_run_id", sa.String(length=80), nullable=False),
        sa.Column("manifest", sa.JSON(), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.ForeignKeyConstraint(["simulation_run_id"], ["simulation_runs.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("simulation_run_id"),
    )


def downgrade() -> None:
    op.drop_table("simulation_artifacts")
    op.drop_index("ix_simulation_runs_semantic_status", table_name="simulation_runs")
    op.drop_index("ix_simulation_runs_status", table_name="simulation_runs")
    op.drop_index("ix_simulation_runs_requested_by", table_name="simulation_runs")
    op.drop_table("simulation_runs")
