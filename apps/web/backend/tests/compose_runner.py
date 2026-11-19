"""Ejecuta pytest y publica salud hasta que termine la aceptación E2E."""

from http.server import BaseHTTPRequestHandler, HTTPServer

import pytest


class HealthHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802
        body = b'{"status":"tests-passed"}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_: object) -> None:
        return


if __name__ == "__main__":
    result = pytest.main(["-q", "tests", "--ignore=tests/compose_runner.py"])
    if result:
        raise SystemExit(result)
    HTTPServer(("0.0.0.0", 8091), HealthHandler).serve_forever()
