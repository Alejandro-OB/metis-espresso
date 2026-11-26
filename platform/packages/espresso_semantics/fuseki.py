"""Cliente mínimo para grafos nombrados y consultas permitidas de Fuseki."""

from __future__ import annotations

import base64
import json
from dataclasses import dataclass
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from rdflib import Graph


class FusekiError(RuntimeError):
    pass


@dataclass(frozen=True)
class FusekiClient:
    dataset_url: str
    username: str | None = None
    password: str | None = None
    timeout_seconds: float = 10.0

    def _authorization(self) -> str | None:
        if self.username is None:
            return None
        token = base64.b64encode(f"{self.username}:{self.password or ''}".encode()).decode()
        return f"Basic {token}"

    def _request(self, request: Request) -> bytes:
        authorization = self._authorization()
        if authorization:
            request.add_header("Authorization", authorization)
        try:
            with urlopen(request, timeout=self.timeout_seconds) as response:  # noqa: S310
                return response.read()
        except (HTTPError, URLError, TimeoutError) as error:
            raise FusekiError(f"Fuseki no respondió correctamente: {error}") from error

    def replace_graph(self, graph_name: str, graph: Graph) -> None:
        url = f"{self.dataset_url.rstrip('/')}/data?{urlencode({'graph': graph_name})}"
        request = Request(
            url,
            data=graph.serialize(format="turtle", encoding="utf-8"),
            method="PUT",
            headers={"Content-Type": "text/turtle; charset=utf-8"},
        )
        self._request(request)

    def query(self, sparql: str) -> dict[str, object]:
        request = Request(
            f"{self.dataset_url.rstrip('/')}/query",
            data=urlencode({"query": sparql}).encode(),
            method="POST",
            headers={"Accept": "application/sparql-results+json"},
        )
        return json.loads(self._request(request))

    def ping(self) -> bool:
        try:
            self.query("ASK { ?s ?p ?o }")
        except FusekiError:
            return False
        return True

    def evidence_for(self, resource_iri: str, graph_iri: str) -> list[dict[str, object]]:
        query = f"""
        SELECT ?predicate ?value WHERE {{
          GRAPH <{graph_iri}> {{ <{resource_iri}> ?predicate ?value }}
        }} ORDER BY ?predicate ?value
        """
        bindings = self.query(query).get("results", {}).get("bindings", [])
        return [
            {
                "id": f"fact:{index}:{item['predicate']['value']}",
                "predicate": item["predicate"]["value"],
                "value": item["value"]["value"],
                "origin": "canonical-graph",
            }
            for index, item in enumerate(bindings)
        ]
