from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any


class SemanticManifest:
    def __init__(self, path: Path, project_root: Path) -> None:
        self.path = path
        self.project_root = project_root
        self.data: dict[str, Any] = json.loads(path.read_text(encoding="utf-8"))

    def verify(self) -> None:
        for name, item in self.data.items():
            if name == "schemaVersion":
                continue
            path = self.project_root / item["path"]
            actual = hashlib.sha256(path.read_bytes()).hexdigest()
            if actual != item["sha256"]:
                raise ValueError(f"checksum semántico inválido para {item['path']}")
