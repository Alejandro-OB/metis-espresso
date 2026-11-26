"""Esquema documental trazable inicial.

Revision ID: 0001
"""

import sqlalchemy as sa

from alembic import op

revision = "0001"
down_revision = None
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        "documents",
        sa.Column("kind", sa.String(length=40), nullable=False),
        sa.Column("identifier", sa.String(length=80), nullable=False),
        sa.Column("payload", sa.JSON(), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
        sa.PrimaryKeyConstraint("kind", "identifier"),
    )
    op.create_table(
        "messages",
        sa.Column("message_id", sa.String(length=80), nullable=False),
        sa.Column("message_type", sa.String(length=60), nullable=False),
        sa.Column("device_id", sa.String(length=120), nullable=False),
        sa.Column("sequence", sa.Integer(), nullable=False),
        sa.Column("occurred_at", sa.String(length=40), nullable=False),
        sa.Column("payload", sa.JSON(), nullable=False),
        sa.Column("received_at", sa.DateTime(timezone=True), nullable=False),
        sa.PrimaryKeyConstraint("message_id"),
    )
    op.create_index("ix_messages_message_type", "messages", ["message_type"])
    op.create_index("ix_messages_device_id", "messages", ["device_id"])
    op.create_table(
        "audit_records",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("actor", sa.String(length=120), nullable=False),
        sa.Column("operation", sa.String(length=120), nullable=False),
        sa.Column("resource", sa.String(length=160), nullable=False),
        sa.Column("result", sa.String(length=40), nullable=False),
        sa.Column("correlation_id", sa.String(length=80), nullable=False),
        sa.Column("occurred_at", sa.DateTime(timezone=True), nullable=False),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("ix_audit_records_actor", "audit_records", ["actor"])
    op.create_index("ix_audit_records_correlation_id", "audit_records", ["correlation_id"])


def downgrade() -> None:
    op.drop_table("audit_records")
    op.drop_table("messages")
    op.drop_table("documents")
