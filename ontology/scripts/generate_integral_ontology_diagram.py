#!/usr/bin/env python3
"""Genera un mapa integral de la ontología a partir de espresso-dt.ttl.

La fuente semántica es exclusivamente el archivo Turtle. El resultado combina
un recorrido conceptual con catálogos exhaustivos de axiomas, de modo que una
sola lámina conserve legibilidad aun cuando muestre toda la terminología.
"""

from __future__ import annotations

import html
import math
import subprocess
import textwrap
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

from rdflib import BNode, Graph, OWL, RDF, RDFS, URIRef


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "owl" / "espresso-dt.ttl"
OUT_DIR = ROOT / "diagrams" / "ontologia"
BASE = OUT_DIR / "ontologia-diagrama-integral"

EDT = "https://w3id.org/espresso-dt#"

CANVAS_W = 7200
MARGIN = 100
GAP = 32
FONT = "DejaVu Sans"

COLORS = {
    "core": ("#EEF2F6", "#64748B", "#334155"),
    "process": ("#EAF2FF", "#3B82F6", "#1D4ED8"),
    "physical": ("#E8F7F1", "#10B981", "#047857"),
    "profile": ("#FFF4E5", "#F59E0B", "#B45309"),
    "observation": ("#E6F7FB", "#06B6D4", "#0E7490"),
    "sensory": ("#F1EAFF", "#8B5CF6", "#6D28D9"),
    "knowledge": ("#FFF0E8", "#F97316", "#C2410C"),
    "context": ("#ECFDF5", "#22C55E", "#15803D"),
}


def local(term: URIRef | BNode) -> str:
    value = str(term)
    if value.startswith(EDT):
        return value[len(EDT) :]
    if "#" in value:
        return value.rsplit("#", 1)[1]
    return value.rsplit("/", 1)[-1]


def class_expression(graph: Graph, term: URIRef | BNode) -> str:
    """Renderiza clases nombradas y uniones OWL sin exponer identificadores BNode."""
    if isinstance(term, URIRef):
        return local(term)

    head = graph.value(term, OWL.unionOf)
    if head is not None:
        members = []
        while head and head != RDF.nil:
            member = graph.value(head, RDF.first)
            if member is not None:
                members.append(class_expression(graph, member))
            head = graph.value(head, RDF.rest)
        if members:
            return " ⊔ ".join(members)

    return "expresión OWL anónima"


def es_label(graph: Graph, term: URIRef) -> str:
    labels = [str(v) for v in graph.objects(term, RDFS.label) if v.language == "es"]
    return labels[0] if labels else local(term)


def first_type_name(graph: Graph, subject: URIRef) -> str:
    names = [local(v) for v in graph.objects(subject, RDF.type) if str(v).startswith(EDT)]
    return sorted(names)[0] if names else ""


def parse_graphs() -> tuple[Graph, Graph]:
    text = SOURCE.read_text(encoding="utf-8")
    schema_text = text.split("# Synthetic population for validation", 1)[0]
    full = Graph().parse(data=text, format="turtle")
    schema = Graph().parse(data=schema_text, format="turtle")
    return full, schema


def class_groups(graph: Graph) -> list[tuple[str, str, list[str]]]:
    groups = [
        ("Núcleo ontológico", "core", [
            "EspressoEntity", "ProcessEntity", "MaterialEntity", "EquipmentEntity",
            "InformationEntity", "QualityEntity", "KnowledgeEntity", "Agent",
        ]),
        ("Proceso de extracción", "process", [
            "EspressoExtraction", "EspressoCalibration",
            "ParameterAdjustment", "ManualParameterAdjustment", "AutomaticParameterAdjustment",
        ]),
        ("Participantes, equipo y material", "physical", [
            "Barista", "SoftwareAgent", "EspressoMachine", "Grinder", "CoffeeMaterial",
            "CoffeeDose", "EspressoBeverage", "CoffeeVariety", "PostHarvestProcessType",
            "RoastLevel", "GrindCoarsenessCategory", "Terroir", "AltitudeSpecification",
        ]),
        ("Perfil y configuración", "profile", [
            "ExtractionProfile", "ProfileParameterSpecification", "ProfileStatus",
            "ValidationStatus", "AdjustmentDirection", "AdjustmentOutcomeCategory",
            "TechnicalConformanceCategory", "SensoryAcceptanceStatus", "TerminationCause", "Unit",
        ]),
        ("Observación y medición", "observation", [
            "Observation", "QualitativeObservation", "QuantitativeObservation",
            "CalculatedObservation", "MeasuredObservation", "QualityIndicatorObservation",
            "EstimatedObservation", "TDSObservation", "ExtractionYieldObservation",
            "ObservationSeries", "ObservableProperty", "QualityIndicator",
        ]),
        ("Evaluación sensorial", "sensory", [
            "SensoryProfile", "DeclaredSensoryProfile", "TargetSensoryProfile",
            "SensoryEvaluation", "SensoryScore", "PerceivedIntensityScore",
            "HedonicLikingScore", "SensoryAttribute", "SensoryDimension",
            "SensoryDescriptor", "FruityDescriptor", "SweetDescriptor", "FloralDescriptor",
            "OtherSensoryDescriptor", "SensoryDefect", "BasicTaste", "MouthfeelTexture",
            "DesiredSensoryChange",
            "DesiredChangeDirection",
        ]),
        ("Conocimiento y decisión", "knowledge", [
            "GuidanceKnowledge", "EvaluationCriterion", "ExpertHeuristic", "DiagnosticHeuristic",
            "Procedure", "DiagnosticProcedure", "CalibrationProcedure",
            "ContextualEvidencePattern", "ValidationPolicy", "Rule",
            "TechnicalDeviation", "TechnicalConformanceAssessment", "Diagnosis",
            "AdjustmentOutcomeAssessment", "UnderExtractionDiagnosis", "DemonstratedCause",
            "AdjustmentAction", "RecommendedAction", "KnowledgeProfile",
            "LocalKnowledgeProfile", "GlobalKnowledgeProfile", "ObservedDifference",
            "ExplanationAssertion",
        ]),
        ("Contexto", "context", [
            "ContextAssertion", "StaticContextAssertion", "DynamicContextAssertion",
            "EnvironmentalCondition",
        ]),
    ]
    expected = {local(c) for c in graph.subjects(RDF.type, OWL.Class) if str(c).startswith(EDT)}
    assigned = {name for _, _, names in groups for name in names}
    if expected != assigned:
        raise RuntimeError(
            f"Clasificación incompleta. Faltan={sorted(expected-assigned)}; sobran={sorted(assigned-expected)}"
        )
    return groups


OBJECT_GROUPS = {
    "Perfil y configuración": [
        "usesProfile", "definedBy", "validatedBy", "applicableToCoffee", "supportedBy",
        "revisionOf", "hasProfileStatus", "hasParameterSpecification", "forProperty",
    ],
    "Extracción y resultado": [
        "producesBeverage", "producedByExtraction", "performedBy",
        "usesEquipment", "usesMaterial", "hasObservation",
        "hasObservationSeries", "hasSensoryEvaluation", "hasRecommendedAction", "evaluatesOutcomeOf",
        "hasTerminationCause",
    ],
    "Observación y medición": [
        "seriesForProperty", "derivedFromSeries", "observedProperty", "featureOfInterest", "recordedBy",
        "hasUnit", "computedFrom", "derivedFromSpecification", "evaluatesBeverage", "evaluatedBy", "hasSensoryScore",
        "forSensoryAttribute", "measuresDimension",
    ],
    "Calibración y ajuste": [
        "hasTrialExtraction", "hasParameterAdjustment", "partOfCalibration", "validatesProfile",
        "producesProfile", "changesProperty", "targetsOutcome", "basedOnRecommendation", "precedes",
        "allowsManualAdjustmentOf", "allowsAutomaticAdjustmentOf",
    ],
    "Conocimiento y explicación": [
        "derivedFrom", "respondsTo", "hasKnowledgeBasis", "basedOnCriterion",
        "supportedByHeuristic", "followsProcedure", "supportedByEvidencePattern",
        "governedByPolicy", "recommendedByRule", "adjustsProperty", "assertsKnowledge",
        "generatedBy", "hasValidationStatus", "hasEvidence",
        "isDiagnosisOf", "explains", "generatedByRule", "comparesExtraction",
        "recommendedDirection", "assessesAdjustment", "hasOutcomeCategory",
        "assessesExtraction", "againstProfile", "hasTechnicalConformance",
    ],
    "Contexto, café y sensorial": [
        "occurredUnder", "hasSensoryDescriptor", "hasPerceivedDefect", "hasMainTaste",
        "hasMouthfeelTexture",
        "hasDesiredSensoryChange", "targetsSensoryAttribute", "desiredDirection", "hasVariety",
        "hasPostHarvestProcess", "hasRoastLevel", "hasTerroir", "hasAltitude",
        "hasGrindCoarsenessCategory", "hasSensoryAcceptanceStatus",
        "hasDeclaredSensoryProfile", "hasTargetSensoryProfile", "describesCoffee",
        "declaredBy", "sensoryTargetDefinedBy", "comparesWithSensoryProfile",
    ],
}


def object_relations(graph: Graph) -> list[tuple[str, str, list[str]]]:
    properties = {
        local(p): p for p in graph.subjects(RDF.type, OWL.ObjectProperty) if str(p).startswith(EDT)
    }
    assigned = {name for names in OBJECT_GROUPS.values() for name in names}
    if set(properties) != assigned:
        raise RuntimeError(
            f"Propiedades de objeto sin clasificar={sorted(set(properties)-assigned)}; "
            f"desconocidas={sorted(assigned-set(properties))}"
        )

    group_colors = {
        "Perfil y configuración": "profile",
        "Extracción y resultado": "process",
        "Observación y medición": "observation",
        "Calibración y ajuste": "process",
        "Conocimiento y explicación": "knowledge",
        "Contexto, café y sensorial": "sensory",
    }
    result = []
    for title, names in OBJECT_GROUPS.items():
        lines = []
        for name in names:
            prop = properties[name]
            domains = [class_expression(graph, v) for v in graph.objects(prop, RDFS.domain)]
            ranges = [class_expression(graph, v) for v in graph.objects(prop, RDFS.range)]
            source = " + ".join(domains) if domains else "∅dom"
            target = " + ".join(ranges) if ranges else "∅ran"
            tags = []
            if (prop, RDF.type, OWL.FunctionalProperty) in graph:
                tags.append("F")
            if (prop, RDF.type, OWL.InverseFunctionalProperty) in graph:
                tags.append("IF")
            if str(graph.value(prop, OWL.deprecated) or "").lower() == "true":
                tags.append("obsoleta")
            inverses = [local(v) for v in graph.objects(prop, OWL.inverseOf)]
            suffix = f"  [{' '.join(tags)}]" if tags else ""
            if inverses:
                suffix += f"  [inv: {', '.join(inverses)}]"
            lines.append(f"{source}  — {name} →  {target}{suffix}")
        result.append((title, group_colors[title], lines))
    return result


def datatype_relations(graph: Graph) -> list[str]:
    props = sorted(
        [p for p in graph.subjects(RDF.type, OWL.DatatypeProperty) if str(p).startswith(EDT)],
        key=local,
    )
    lines = []
    for prop in props:
        domains = [class_expression(graph, v) for v in graph.objects(prop, RDFS.domain)]
        ranges = [class_expression(graph, v) for v in graph.objects(prop, RDFS.range)]
        source = " + ".join(domains) if domains else "∅dom"
        target = " + ".join(ranges) if ranges else "∅ran"
        functional = "  [F]" if (prop, RDF.type, OWL.FunctionalProperty) in graph else ""
        lines.append(f"{source}  — {local(prop)} →  {target}{functional}")
    return lines


def restriction_lines(graph: Graph) -> list[str]:
    lines = []
    for cls in sorted(
        [c for c in graph.subjects(RDF.type, OWL.Class) if str(c).startswith(EDT)], key=local
    ):
        for restriction in graph.objects(cls, RDFS.subClassOf):
            if not isinstance(restriction, BNode) or (restriction, RDF.type, OWL.Restriction) not in graph:
                continue
            prop = graph.value(restriction, OWL.onProperty)
            if prop is None:
                continue
            target = graph.value(restriction, OWL.onClass) or graph.value(restriction, OWL.someValuesFrom)
            forms = [
                (OWL.qualifiedCardinality, "exactly"),
                (OWL.minQualifiedCardinality, "min"),
                (OWL.maxQualifiedCardinality, "max"),
                (OWL.cardinality, "exactly"),
                (OWL.minCardinality, "min"),
                (OWL.maxCardinality, "max"),
            ]
            rendered = None
            for predicate, word in forms:
                value = graph.value(restriction, predicate)
                if value is not None:
                    rendered = f"{word} {value}"
                    break
            if rendered is None and graph.value(restriction, OWL.someValuesFrom) is not None:
                rendered = "some"
            target_text = f" {local(target)}" if target is not None else ""
            lines.append(f"{local(cls)}: {local(prop)} {rendered}{target_text}")
    return lines


def disjoint_lines(graph: Graph) -> list[str]:
    lines = []
    for axiom in graph.subjects(RDF.type, OWL.AllDisjointClasses):
        head = graph.value(axiom, OWL.members)
        members = []
        while head and head != RDF.nil:
            members.append(local(graph.value(head, RDF.first)))
            head = graph.value(head, RDF.rest)
        if members:
            lines.extend(textwrap.wrap("Mutuamente disjuntas: " + ", ".join(members), 105))
    seen = set()
    for left, right in graph.subject_objects(OWL.disjointWith):
        pair = tuple(sorted((local(left), local(right))))
        if pair not in seen:
            seen.add(pair)
            lines.append(f"{pair[0]}  ⟂  {pair[1]}")
    return lines


def vocabulary_lines(graph: Graph) -> list[str]:
    vocab_types = [
        "ObservableProperty", "QualityIndicator", "SensoryAttribute", "SensoryDimension",
        "FruityDescriptor", "SweetDescriptor", "FloralDescriptor", "OtherSensoryDescriptor",
        "SensoryDefect", "DesiredChangeDirection", "AdjustmentDirection",
        "GrindCoarsenessCategory", "ProfileStatus", "ValidationStatus", "TerminationCause", "Unit",
    ]
    lines = []
    for type_name in vocab_types:
        cls = URIRef(EDT + type_name)
        values = sorted(local(s) for s in graph.subjects(RDF.type, cls) if isinstance(s, URIRef))
        if not values:
            continue
        wrapped = textwrap.wrap(", ".join(values), width=86, subsequent_indent="    ")
        lines.append(f"{type_name}: {wrapped[0]}")
        lines.extend(wrapped[1:])
    return lines


def hierarchy_items(graph: Graph, class_names: list[str]) -> list[str]:
    lines = []
    for name in class_names:
        cls = URIRef(EDT + name)
        parents = sorted(
            local(v)
            for v in graph.objects(cls, RDFS.subClassOf)
            if isinstance(v, URIRef) and str(v).startswith(EDT)
        )
        parent = " ⊑ " + " ∩ ".join(parents) if parents else ""
        label = es_label(graph, cls)
        lines.append(f"{name}{parent}  ·  {label}")
    return lines


class Svg:
    def __init__(self, width: int, height: int):
        self.width = width
        self.height = height
        self.parts = [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
            "<defs>",
            '<filter id="shadow" x="-10%" y="-10%" width="120%" height="130%"><feDropShadow dx="0" dy="5" stdDeviation="9" flood-color="#0F172A" flood-opacity="0.10"/></filter>',
            '<marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto"><path d="M 0 0 L 10 5 L 0 10 z" fill="#475569"/></marker>',
            "</defs>",
            f'<rect width="{width}" height="{height}" fill="#FFFFFF"/>',
        ]

    def rect(self, x, y, w, h, fill="#fff", stroke="#CBD5E1", sw=2, r=18, shadow=False, dash=None):
        filt = ' filter="url(#shadow)"' if shadow else ""
        dashed = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{dashed}{filt}/>'
        )

    def line(self, x1, y1, x2, y2, color="#475569", sw=3, arrow=True, dash=None):
        marker = ' marker-end="url(#arrow)"' if arrow else ""
        dashed = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(
            f'<path d="M {x1} {y1} L {x2} {y2}" fill="none" stroke="{color}" stroke-width="{sw}"{marker}{dashed}/>'
        )

    def path(self, d, color="#475569", sw=3, arrow=True, dash=None):
        marker = ' marker-end="url(#arrow)"' if arrow else ""
        dashed = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(
            f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{sw}"{marker}{dashed}/>'
        )

    def text(self, x, y, value, size=24, color="#17212B", weight=400, anchor="start", family=FONT):
        self.parts.append(
            f'<text x="{x}" y="{y}" font-family="{family}" font-size="{size}" font-weight="{weight}" fill="{color}" text-anchor="{anchor}">{html.escape(str(value))}</text>'
        )

    def close(self) -> str:
        return "\n".join(self.parts + ["</svg>"])


class Drawio:
    def __init__(self, width: int, height: int):
        self.width = width
        self.height = height
        self.next_id = 2
        self.bounds = {}
        self.native_edge_labels = False
        self.native_edge_label_size = None
        self.force_orthogonal_edges = False
        self.root = ET.Element("root")
        ET.SubElement(self.root, "mxCell", id="0")
        ET.SubElement(self.root, "mxCell", id="1", parent="0")

    def _id(self) -> str:
        value = str(self.next_id)
        self.next_id += 1
        return value

    def vertex(self, value, x, y, w, h, style, parent="1") -> str:
        cid = self._id()
        cell = ET.SubElement(
            self.root, "mxCell", id=cid, value=value, style=style, vertex="1", parent=parent
        )
        ET.SubElement(cell, "mxGeometry", x=str(x), y=str(y), width=str(w), height=str(h), **{"as": "geometry"})
        self.bounds[cid] = (float(x), float(y), float(w), float(h))
        return cid

    def edge(
        self,
        value,
        source,
        target,
        style=None,
        *,
        points=None,
        source_point=None,
        target_point=None,
        label_offset=None,
    ) -> str:
        cid = self._id()
        style = style or (
            "edgeStyle=orthogonalEdgeStyle;rounded=1;orthogonalLoop=1;jettySize=auto;"
            "html=1;endArrow=block;endFill=1;strokeWidth=2;strokeColor=#475569;"
            "fontSize=16;fontColor=#334155;labelBackgroundColor=#FFFFFF;"
        )
        cell = ET.SubElement(
            self.root, "mxCell", id=cid, value=value, style=style, edge="1", parent="1",
            source=source, target=target,
        )
        geometry = ET.SubElement(cell, "mxGeometry", relative="1", **{"as": "geometry"})
        if points:
            array = ET.SubElement(geometry, "Array", **{"as": "points"})
            for x, y in points:
                ET.SubElement(array, "mxPoint", x=str(x), y=str(y))
        if label_offset:
            ET.SubElement(
                geometry, "mxPoint", x=str(label_offset[0]), y=str(label_offset[1]),
                **{"as": "offset"},
            )
        if source_point:
            ET.SubElement(
                geometry, "mxPoint", x=str(source_point[0]), y=str(source_point[1]),
                **{"as": "sourcePoint"},
            )
        if target_point:
            ET.SubElement(
                geometry, "mxPoint", x=str(target_point[0]), y=str(target_point[1]),
                **{"as": "targetPoint"},
            )
        return cid

    def xml(self) -> str:
        model = ET.Element(
            "mxGraphModel", dx="1422", dy="794", grid="1", gridSize="10", guides="1",
            tooltips="1", connect="1", arrows="1", fold="1", page="1", pageScale="1",
            pageWidth=str(self.width), pageHeight=str(self.height), background="#FFFFFF",
            math="0", shadow="0",
        )
        model.append(self.root)
        diagram = ET.Element("diagram", id="integral", name="Ontología integral")
        diagram.append(model)
        mxfile = ET.Element(
            "mxfile", host="Electron", agent="Codex", version="26.0.16",
            type="device", compressed="false",
        )
        mxfile.append(diagram)
        return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(mxfile, encoding="unicode")


def add_card(svg: Svg, dio: Drawio, x, y, w, title, items, color_key, line_h=30, font_size=18):
    fill, stroke, dark = COLORS[color_key]
    h = 76 + len(items) * line_h + 24
    svg.rect(x, y, w, h, "#FFFFFF", stroke, 2, 18, shadow=True)
    svg.rect(x, y, w, 62, fill, stroke, 2, 18)
    svg.text(x + 24, y + 40, title, 24, dark, 700)
    for index, item in enumerate(items):
        yy = y + 94 + index * line_h
        svg.text(x + 24, yy, item, font_size, "#334155", 400, family=FONT)

    label = "<b>" + html.escape(title) + "</b><br><br>" + "<br>".join(html.escape(v) for v in items)
    style = (
        f"rounded=1;whiteSpace=wrap;html=1;align=left;verticalAlign=top;spacing=20;"
        f"fillColor=#FFFFFF;strokeColor={stroke};strokeWidth=2;fontSize={font_size};"
        "fontColor=#334155;shadow=1;"
    )
    dio.vertex(label, x, y, w, h, style)
    return h


def add_flow_node(svg: Svg, dio: Drawio, key, x, y, w, h, title, subtitle, color_key):
    fill, stroke, dark = COLORS[color_key]
    svg.rect(x, y, w, h, fill, stroke, 3, 20, shadow=True)
    svg.text(x + w / 2, y + 45, title, 24, dark, 700, "middle")
    svg.text(x + w / 2, y + 77, subtitle, 17, "#475569", 400, "middle")
    label = f"<b>{html.escape(title)}</b><br><font color='#475569'>{html.escape(subtitle)}</font>"
    style = (
        f"rounded=1;whiteSpace=wrap;html=1;fillColor={fill};strokeColor={stroke};"
        f"strokeWidth=3;fontSize=20;fontColor={dark};shadow=1;"
    )
    return dio.vertex(label, x, y, w, h, style)


def add_svg_edge_label(svg: Svg, x, y, text_value, size=16):
    width = max(120, len(text_value) * size * 0.57 + 24)
    svg.rect(x - width / 2, y - 22, width, 32, "#FFFFFF", "#E2E8F0", 1, 8)
    svg.text(x, y, text_value, size, "#334155", 600, "middle")


def build() -> None:
    full, schema = parse_graphs()
    # La versión sale de la cabecera del Turtle: escrita a mano se quedó en
    # 3.2.0 durante tres versiones mientras el mapa sí cambiaba.
    version = str(full.value(URIRef("https://w3id.org/espresso-dt"), OWL.versionInfo))
    groups = class_groups(schema)
    rel_groups = object_relations(schema)
    data_lines = datatype_relations(schema)
    restrictions = restriction_lines(schema)
    disjoints = disjoint_lines(schema)
    vocab = vocabulary_lines(schema)

    class_cards = []
    for title, color, names in groups:
        class_cards.append((title, color, hierarchy_items(schema, names)))

    # Altura calculada: cabecera + flujo + clases (2 filas) + relaciones (2 filas) + detalles.
    header_h = 190
    flow_h = 770
    section_gap = 54
    card_w4 = (CANVAS_W - 2 * MARGIN - 3 * GAP) / 4
    class_heights = [76 + len(items) * 30 + 24 for _, _, items in class_cards]
    class_row1_h = max(class_heights[:4])
    class_row2_h = max(class_heights[4:])

    card_w3 = (CANVAS_W - 2 * MARGIN - 2 * GAP) / 3
    rel_heights = [76 + len(items) * 31 + 24 for _, _, items in rel_groups]
    rel_row1_h = max(rel_heights[:3])
    rel_row2_h = max(rel_heights[3:])

    details_h = max(
        76 + math.ceil(len(data_lines) / 2) * 31 + 24,
        76 + (len(restrictions) + len(disjoints) + 2) * 31 + 24,
        76 + len(vocab) * 28 + 24,
    )

    total_h = int(
        header_h + flow_h + section_gap + class_row1_h + GAP + class_row2_h
        + section_gap + rel_row1_h + GAP + rel_row2_h + section_gap + details_h + 120
    )
    svg = Svg(CANVAS_W, total_h)
    dio = Drawio(CANVAS_W, total_h)

    # Header
    svg.text(MARGIN, 74, "ONTOLOGÍA DEL PROCESO DE EXTRACCIÓN DE CAFÉ ESPRESSO", 38, "#0F172A", 800)
    svg.text(MARGIN, 119, f"Mapa integral de clases, relaciones, restricciones y vocabulario · versión {version}", 23, "#475569", 400)
    svg.text(CANVAS_W - MARGIN, 74, "Fuente única: owl/espresso-dt.ttl", 18, "#64748B", 500, "end")
    legend = "⊑ rdfs:subClassOf   ·   → dirección de la propiedad   ·   ∅dom sin rdfs:domain   ·   F funcional   ·   IF inversa funcional"
    svg.text(MARGIN, 164, legend, 18, "#64748B", 400)
    dio.vertex(
        "<b>ONTOLOGÍA DEL PROCESO DE EXTRACCIÓN DE CAFÉ ESPRESSO</b><br>"
        f"<font color='#475569'>Mapa integral de clases, relaciones, restricciones y vocabulario · versión {version}</font>",
        MARGIN, 28, CANVAS_W - 2 * MARGIN, 110,
        "text;html=1;align=left;verticalAlign=middle;whiteSpace=wrap;fontSize=28;fontColor=#0F172A;strokeColor=none;fillColor=none;",
    )
    dio.vertex(legend, MARGIN, 140, CANVAS_W - 2 * MARGIN, 35,
               "text;html=1;align=left;verticalAlign=middle;fontSize=16;fontColor=#64748B;strokeColor=none;fillColor=none;")

    # Recorrido principal
    y0 = header_h
    svg.rect(MARGIN, y0, CANVAS_W - 2 * MARGIN, flow_h - 30, "#FAFAF9", "#E2E8F0", 2, 24)
    svg.text(MARGIN + 32, y0 + 50, "1 · RECORRIDO SEMÁNTICO PRINCIPAL", 24, "#0F172A", 800)
    svg.text(MARGIN + 32, y0 + 85, "La dirección de cada flecha coincide con la propiedad definida en OWL.", 18, "#64748B", 400)
    dio.vertex(
        "<b>1 · RECORRIDO SEMÁNTICO PRINCIPAL</b><br><font color='#64748B'>La dirección de cada flecha coincide con la propiedad definida en OWL.</font>",
        MARGIN, y0, CANVAS_W - 2 * MARGIN, 95,
        "text;html=1;align=left;verticalAlign=middle;spacingLeft=28;fontSize=20;fontColor=#0F172A;strokeColor=#E2E8F0;fillColor=#FAFAF9;rounded=1;",
    )

    node_y = y0 + 175
    node_w = 710
    node_h = 104
    xs = [180, 1010, 1840, 2670, 3500, 4330, 5160]
    nodes = {}
    nodes["barista"] = add_flow_node(svg, dio, "barista", xs[0], node_y, 560, node_h, "Barista", "Agent", "physical")
    nodes["profile"] = add_flow_node(svg, dio, "profile", xs[1], node_y, node_w, node_h, "ExtractionProfile", "InformationEntity", "profile")
    nodes["extraction"] = add_flow_node(svg, dio, "extraction", xs[2], node_y, node_w, node_h, "EspressoExtraction", "ProcessEntity", "process")
    nodes["observation"] = add_flow_node(svg, dio, "observation", xs[3], node_y, node_w, node_h, "Observation", "InformationEntity", "observation")
    nodes["deviation"] = add_flow_node(svg, dio, "deviation", xs[4], node_y, node_w, node_h, "TechnicalDeviation", "KnowledgeEntity", "knowledge")
    nodes["diagnosis"] = add_flow_node(svg, dio, "diagnosis", xs[5], node_y, node_w, node_h, "Diagnosis", "KnowledgeEntity", "knowledge")
    nodes["explanation"] = add_flow_node(svg, dio, "explanation", xs[6], node_y, node_w, node_h, "ExplanationAssertion", "InformationEntity", "knowledge")

    # Flechas directas del eje principal.
    edges = [
        ("profile", "barista", "definedBy", xs[1], xs[0] + 560, node_y + 30),
        ("extraction", "profile", "usesProfile", xs[2], xs[1] + node_w, node_y + 72),
        ("extraction", "observation", "hasObservation", xs[2] + node_w, xs[3], node_y + 52),
        ("deviation", "observation", "derivedFrom", xs[4], xs[3] + node_w, node_y + 52),
        ("diagnosis", "deviation", "derivedFrom", xs[5], xs[4] + node_w, node_y + 52),
        ("explanation", "diagnosis", "explains", xs[6], xs[5] + node_w, node_y + 52),
    ]
    for source, target, label, x1, x2, yy in edges:
        svg.line(x1, yy, x2, yy, "#475569", 3, True)
        add_svg_edge_label(svg, (x1 + x2) / 2, yy - 12, label)
        dio.edge(label, nodes[source], nodes[target])

    lower_y = node_y + 270
    secondary_w = 650
    secondary = [
        ("material", 300, "CoffeeMaterial", "MaterialEntity", "physical"),
        ("equipment", 1140, "EquipmentEntity", "EspressoEntity", "physical"),
        ("context", 1980, "EnvironmentalCondition", "DynamicContextAssertion", "context"),
        ("sensory", 2820, "SensoryEvaluation", "InformationEntity", "sensory"),
        ("recommended", 3660, "RecommendedAction", "AdjustmentAction", "knowledge"),
        ("guidance", 4500, "GuidanceKnowledge", "criterio · heurística · procedimiento · patrón · política", "knowledge"),
        ("adjustment", 5340, "ParameterAdjustment", "ProcessEntity", "process"),
        ("calibration", 6180, "EspressoCalibration", "ProcessEntity", "process"),
    ]
    sec_ids = {}
    for key, xx, title, subtitle, color in secondary:
        sec_ids[key] = add_flow_node(svg, dio, key, xx, lower_y, secondary_w, node_h, title, subtitle, color)

    # Conexiones secundarias, trazadas fuera del eje principal.
    ex_mid = xs[2] + node_w / 2
    ex_bottom = node_y + node_h
    for key, xx, label in [
        ("material", 300, "usesMaterial"), ("equipment", 1140, "usesEquipment"),
        ("context", 1980, "occurredUnder"), ("sensory", 2820, "hasSensoryEvaluation"),
    ]:
        target_x = xx + secondary_w / 2
        svg.path(f"M {ex_mid} {ex_bottom} C {ex_mid} {lower_y-60}, {target_x} {lower_y-60}, {target_x} {lower_y}", "#64748B", 2.5)
        add_svg_edge_label(svg, target_x, lower_y - 20, label, 14)
        dio.edge(label, nodes["extraction"], sec_ids[key])
    # Ruta de conocimiento y actuación. Todas las flechas respetan el dominio → rango OWL.
    svg.path(
        f"M {ex_mid} {ex_bottom} C {ex_mid+320} {lower_y-85}, {3660+secondary_w/2} {lower_y-85}, {3660+secondary_w/2} {lower_y}",
        "#64748B", 2.5,
    )
    add_svg_edge_label(svg, 3460, lower_y - 69, "hasRecommendedAction", 14)
    dio.edge("hasRecommendedAction", nodes["extraction"], sec_ids["recommended"])

    svg.path(
        f"M {3660+secondary_w/2} {lower_y} C {3660+secondary_w/2} {lower_y-70}, {xs[5]+355} {node_y+node_h+70}, {xs[5]+355} {node_y+node_h}",
        "#64748B", 2.5,
    )
    add_svg_edge_label(svg, 4050, lower_y - 38, "respondsTo", 14)
    dio.edge("respondsTo", sec_ids["recommended"], nodes["diagnosis"])

    svg.line(3660 + secondary_w, lower_y + 38, 4500, lower_y + 38, "#64748B", 2.5, True)
    add_svg_edge_label(svg, 4405, lower_y + 21, "hasKnowledgeBasis", 14)
    dio.edge("hasKnowledgeBasis", sec_ids["recommended"], sec_ids["guidance"])

    generator = add_flow_node(
        svg, dio, "generator", 4500, lower_y + 155, secondary_w, node_h,
        "Agent", "barista o software generador", "physical",
    )
    svg.path(
        f"M {3660+secondary_w/2} {lower_y+node_h} C {3660+secondary_w/2} {lower_y+205}, "
        f"{4500+secondary_w/2} {lower_y+110}, {4500+secondary_w/2} {lower_y+155}",
        "#64748B", 2.5,
    )
    add_svg_edge_label(svg, 4160, lower_y + 148, "generatedBy", 14)
    dio.edge("generatedBy", sec_ids["recommended"], generator)

    svg.path(
        f"M {5340+secondary_w/2} {lower_y+node_h} C {5340+secondary_w/2} {lower_y+160}, "
        f"{3660+secondary_w/2} {lower_y+160}, {3660+secondary_w/2} {lower_y+node_h}",
        "#64748B", 2.5,
    )
    add_svg_edge_label(svg, 4810, lower_y + 154, "basedOnRecommendation", 14)
    dio.edge("basedOnRecommendation", sec_ids["adjustment"], sec_ids["recommended"])

    svg.line(6180, lower_y + 88, 5340 + secondary_w, lower_y + 88, "#64748B", 2.5, True)
    add_svg_edge_label(svg, 6085, lower_y + 72, "hasParameterAdjustment", 14)
    dio.edge("hasParameterAdjustment", sec_ids["calibration"], sec_ids["adjustment"])

    # Clases
    y = header_h + flow_h + section_gap
    svg.text(MARGIN, y - 18, "2 · JERARQUÍA COMPLETA DE CLASES", 25, "#0F172A", 800)
    dio.vertex("<b>2 · JERARQUÍA COMPLETA DE CLASES</b>", MARGIN, y - 48, 2500, 36,
               "text;html=1;align=left;fontSize=22;fontColor=#0F172A;strokeColor=none;fillColor=none;")
    for row in range(2):
        row_cards = class_cards[row * 4 : (row + 1) * 4]
        for col, (title, color, items) in enumerate(row_cards):
            x = MARGIN + col * (card_w4 + GAP)
            add_card(svg, dio, x, y, card_w4, title, items, color, 30, 16)
        y += (class_row1_h if row == 0 else class_row2_h) + GAP

    # Relaciones de objeto
    y += section_gap - GAP
    svg.text(MARGIN, y - 18, "3 · TODAS LAS PROPIEDADES DE OBJETO", 25, "#0F172A", 800)
    dio.vertex("<b>3 · TODAS LAS PROPIEDADES DE OBJETO</b>", MARGIN, y - 48, 2500, 36,
               "text;html=1;align=left;fontSize=22;fontColor=#0F172A;strokeColor=none;fillColor=none;")
    for row in range(2):
        row_cards = rel_groups[row * 3 : (row + 1) * 3]
        for col, (title, color, items) in enumerate(row_cards):
            x = MARGIN + col * (card_w3 + GAP)
            add_card(svg, dio, x, y, card_w3, title, items, color, 31, 16)
        y += (rel_row1_h if row == 0 else rel_row2_h) + GAP

    # Detalles finales
    y += section_gap - GAP
    svg.text(MARGIN, y - 18, "4 · PROPIEDADES DE DATOS, AXIOMAS Y VOCABULARIO", 25, "#0F172A", 800)
    dio.vertex("<b>4 · PROPIEDADES DE DATOS, AXIOMAS Y VOCABULARIO</b>", MARGIN, y - 48, 3200, 36,
               "text;html=1;align=left;fontSize=22;fontColor=#0F172A;strokeColor=none;fillColor=none;")

    half = math.ceil(len(data_lines) / 2)
    data_compact = data_lines[:half] + [""] + data_lines[half:]
    axiom_items = ["Restricciones OWL:"] + restrictions + ["", "Disyunciones:"] + disjoints
    detail_cards = [
        ("Propiedades de datos", "observation", data_compact, 29, 15),
        ("Restricciones y disyunciones", "knowledge", axiom_items, 29, 15),
        ("Vocabulario controlado", "sensory", vocab, 28, 15),
    ]
    for col, (title, color, items, lh, fs) in enumerate(detail_cards):
        x = MARGIN + col * (card_w3 + GAP)
        add_card(svg, dio, x, y, card_w3, title, items, color, lh, fs)

    footer_y = total_h - 52
    svg.text(MARGIN, footer_y, "La lámina representa el esquema intensional. Los individuos sintéticos de validación no se muestran para evitar confundir el modelo con su población.", 17, "#64748B", 400)
    svg.text(CANVAS_W - MARGIN, footer_y, "Generado directamente desde espresso-dt.ttl", 17, "#64748B", 500, "end")
    dio.vertex(
        "La lámina representa el esquema intensional. Los individuos sintéticos de validación no se muestran para evitar confundir el modelo con su población.",
        MARGIN, footer_y - 26, CANVAS_W - 2 * MARGIN, 32,
        "text;html=1;align=left;fontSize=15;fontColor=#64748B;strokeColor=none;fillColor=none;",
    )

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    svg_path = BASE.with_suffix(".svg")
    png_path = BASE.with_suffix(".png")
    drawio_path = BASE.with_suffix(".drawio")
    svg_path.write_text(svg.close(), encoding="utf-8")
    drawio_path.write_text(dio.xml(), encoding="utf-8")
    subprocess.run(
        ["magick", "-background", "white", str(svg_path), str(png_path)],
        check=True,
    )
    print(f"SVG: {svg_path}")
    print(f"PNG: {png_path}")
    print(f"DRAWIO: {drawio_path}")
    print(f"Clases: {sum(len(v) for _, _, v in groups)}")
    print(f"Propiedades de objeto: {sum(len(v) for v in OBJECT_GROUPS.values())}")
    print(f"Propiedades de datos: {len(data_lines)}")
    print(f"Restricciones OWL: {len(restrictions)}")


if __name__ == "__main__":
    build()
