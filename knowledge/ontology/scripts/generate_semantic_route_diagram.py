#!/usr/bin/env python3
"""Genera una lámina independiente del recorrido semántico principal."""

from pathlib import Path

from rdflib import Graph, OWL, RDF, URIRef

from generate_integral_ontology_diagram import (
    Drawio,
    Svg,
    add_flow_node,
    add_svg_edge_label,
)


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ontology" / "espresso-dt.ttl"
OUT_DIR = ROOT / "diagrams" / "ontologia"
BASE = OUT_DIR / "recorrido-semantico-principal"
EDT = "https://w3id.org/espresso-dt#"

WIDTH = 7200
HEIGHT = 1120


def verify_terms() -> None:
    graph = Graph().parse(SOURCE, format="turtle")
    classes = {
        "Barista", "ExtractionProfile", "EspressoExtraction", "Observation", "TechnicalDeviation", "Diagnosis",
        "ExplanationAssertion", "CoffeeMaterial", "EquipmentEntity", "EnvironmentalCondition",
        "SensoryEvaluation", "RecommendedAction", "GuidanceKnowledge", "Agent", "ParameterAdjustment",
        "EspressoCalibration", "AdjustmentOutcomeAssessment", "TechnicalConformanceAssessment",
    }
    properties = {
        "definedBy", "usesProfile", "hasObservation", "derivedFrom", "explains", "isDiagnosisOf",
        "usesMaterial", "usesEquipment", "occurredUnder", "hasSensoryEvaluation",
        "hasRecommendedAction", "respondsTo", "hasKnowledgeBasis", "generatedBy", "basedOnRecommendation",
        "hasParameterAdjustment", "evaluatesOutcomeOf", "targetsOutcome", "assessesAdjustment",
        "assessesExtraction", "againstProfile", "hasSensoryAcceptanceStatus",
    }
    missing_classes = [
        name for name in classes
        if (URIRef(EDT + name), RDF.type, OWL.Class) not in graph
    ]
    missing_properties = [
        name for name in properties
        if (URIRef(EDT + name), RDF.type, OWL.ObjectProperty) not in graph
    ]
    if missing_classes or missing_properties:
        raise RuntimeError(
            f"Términos ausentes en la ontología: clases={missing_classes}, propiedades={missing_properties}"
        )


def build() -> None:
    verify_terms()
    svg = Svg(WIDTH, HEIGHT)
    dio = Drawio(WIDTH, HEIGHT)

    # Cabecera
    svg.text(100, 70, "RECORRIDO SEMÁNTICO PRINCIPAL", 38, "#0F172A", 800)
    svg.text(
        100, 116,
        "Ontología del proceso de extracción de café espresso · versión 3.2.0",
        23, "#475569", 400,
    )
    svg.text(
        WIDTH - 100, 70,
        "La flecha señala la dirección dominio → rango de cada propiedad",
        18, "#64748B", 500, "end",
    )
    dio.vertex(
        "<b>RECORRIDO SEMÁNTICO PRINCIPAL</b><br>"
        "<font color='#475569'>Ontología del proceso de extracción de café espresso · versión 3.2.0</font>",
        100, 25, WIDTH - 200, 100,
        "text;html=1;align=left;verticalAlign=middle;whiteSpace=wrap;fontSize=28;"
        "fontColor=#0F172A;strokeColor=none;fillColor=none;",
    )

    panel_y = 160
    svg.rect(100, panel_y, WIDTH - 200, 850, "#FAFAF9", "#E2E8F0", 2, 24)
    dio.vertex(
        "", 100, panel_y, WIDTH - 200, 850,
        "rounded=1;whiteSpace=wrap;html=1;fillColor=#FAFAF9;strokeColor=#E2E8F0;"
        "strokeWidth=2;",
    )

    # Eje principal
    node_y = 290
    node_h = 106
    node_w = 710
    xs = [180, 1010, 1840, 2670, 3500, 4330, 5160]
    nodes = {}
    nodes["barista"] = add_flow_node(
        svg, dio, "barista", xs[0], node_y, 560, node_h, "Barista", "Agent", "physical"
    )
    nodes["profile"] = add_flow_node(
        svg, dio, "profile", xs[1], node_y, node_w, node_h,
        "ExtractionProfile", "InformationEntity", "profile",
    )
    nodes["extraction"] = add_flow_node(
        svg, dio, "extraction", xs[2], node_y, node_w, node_h,
        "EspressoExtraction", "ProcessEntity", "process",
    )
    nodes["observation"] = add_flow_node(
        svg, dio, "observation", xs[3], node_y, node_w, node_h,
        "Observation", "InformationEntity", "observation",
    )
    nodes["deviation"] = add_flow_node(
        svg, dio, "deviation", xs[4], node_y, node_w, node_h,
        "TechnicalDeviation", "KnowledgeEntity", "knowledge",
    )
    nodes["diagnosis"] = add_flow_node(
        svg, dio, "diagnosis", xs[5], node_y, node_w, node_h,
        "Diagnosis", "KnowledgeEntity", "knowledge",
    )
    nodes["explanation"] = add_flow_node(
        svg, dio, "explanation", xs[6], node_y, node_w, node_h,
        "ExplanationAssertion", "InformationEntity", "knowledge",
    )
    nodes["technical"] = add_flow_node(
        svg, dio, "technical", 6300, node_y, 650, node_h,
        "TechnicalConformanceAssessment", "KnowledgeEntity", "knowledge",
    )

    direct_edges = [
        ("profile", "barista", "definedBy", xs[1], xs[0] + 560, node_y + 30),
        ("extraction", "profile", "usesProfile", xs[2], xs[1] + node_w, node_y + 74),
        ("extraction", "observation", "hasObservation", xs[2] + node_w, xs[3], node_y + 53),
        ("deviation", "observation", "derivedFrom", xs[4], xs[3] + node_w, node_y + 53),
        ("diagnosis", "deviation", "derivedFrom", xs[5], xs[4] + node_w, node_y + 53),
        ("explanation", "diagnosis", "explains", xs[6], xs[5] + node_w, node_y + 53),
    ]
    for source, target, relation, x1, x2, yy in direct_edges:
        svg.line(x1, yy, x2, yy, "#475569", 3, True)
        add_svg_edge_label(svg, (x1 + x2) / 2, yy - 13, relation)
        dio.edge(relation, nodes[source], nodes[target])

    # La conformidad técnica evalúa la extracción contra el perfil completo.
    technical_mid = 6300 + 650 / 2
    extraction_mid = xs[2] + node_w / 2
    profile_mid = xs[1] + node_w / 2
    svg.path(
        f"M {technical_mid} {node_y} C {technical_mid} 215, {extraction_mid} 215, {extraction_mid} {node_y}",
        "#64748B", 2.5,
    )
    add_svg_edge_label(svg, 4380, 208, "assessesExtraction", 14)
    dio.edge("assessesExtraction", nodes["technical"], nodes["extraction"])
    svg.path(
        f"M {technical_mid-80} {node_y} C {technical_mid-80} 180, {profile_mid} 180, {profile_mid} {node_y}",
        "#64748B", 2.5,
    )
    add_svg_edge_label(svg, 3860, 173, "againstProfile", 14)
    dio.edge("againstProfile", nodes["technical"], nodes["profile"])

    # Ramas asociadas al proceso y ciclo de decisión.
    lower_y = 590
    secondary_w = 650
    secondary = [
        ("material", 300, "CoffeeMaterial", "MaterialEntity", "physical"),
        ("equipment", 1140, "EquipmentEntity", "EspressoEntity", "physical"),
        ("context", 1980, "EnvironmentalCondition", "DynamicContextAssertion", "context"),
        ("sensory", 2820, "SensoryEvaluation", "observado · compara perfil declarado y objetivo", "sensory"),
        ("recommended", 3660, "RecommendedAction", "AdjustmentAction", "knowledge"),
        ("outcome", 4500, "AdjustmentOutcomeAssessment", "Diagnosis", "knowledge"),
        ("adjustment", 5340, "ParameterAdjustment", "ProcessEntity · targetsOutcome declarado al evaluarlo", "process"),
        ("calibration", 6180, "EspressoCalibration", "ProcessEntity", "process"),
    ]
    sec_ids = {}
    for key, xx, title, subtitle, color in secondary:
        sec_ids[key] = add_flow_node(
            svg, dio, key, xx, lower_y, secondary_w, node_h, title, subtitle, color
        )

    ex_mid = xs[2] + node_w / 2
    ex_bottom = node_y + node_h
    for key, xx, relation in [
        ("material", 300, "usesMaterial"),
        ("equipment", 1140, "usesEquipment"),
        ("context", 1980, "occurredUnder"),
        ("sensory", 2820, "hasSensoryEvaluation"),
    ]:
        target_x = xx + secondary_w / 2
        svg.path(
            f"M {ex_mid} {ex_bottom} C {ex_mid} {lower_y-70}, "
            f"{target_x} {lower_y-70}, {target_x} {lower_y}",
            "#64748B", 2.5,
        )
        add_svg_edge_label(svg, target_x, lower_y - 27, relation, 14)
        dio.edge(relation, nodes["extraction"], sec_ids[key])

    # La extracción queda enlazada con la recomendación que se genera para el caso.
    svg.path(
        f"M {ex_mid} {ex_bottom} C {ex_mid+320} {lower_y-95}, "
        f"{3660+secondary_w/2} {lower_y-95}, {3660+secondary_w/2} {lower_y}",
        "#64748B", 2.5,
    )
    add_svg_edge_label(svg, 3460, lower_y - 78, "hasRecommendedAction", 14)
    dio.edge("hasRecommendedAction", nodes["extraction"], sec_ids["recommended"])

    # RecommendedAction → Diagnosis
    svg.path(
        f"M {3660+secondary_w/2} {lower_y} C {3660+secondary_w/2} {lower_y-75}, "
        f"{xs[5]+node_w/2} {node_y+node_h+75}, {xs[5]+node_w/2} {node_y+node_h}",
        "#64748B", 2.5,
    )
    add_svg_edge_label(svg, 4050, lower_y - 44, "respondsTo", 14)
    dio.edge("respondsTo", sec_ids["recommended"], nodes["diagnosis"])

    guidance = add_flow_node(
        svg, dio, "guidance", 3660, lower_y + 190, secondary_w, node_h,
        "GuidanceKnowledge", "criterio · heurística · procedimiento · patrón · política", "knowledge",
    )
    generator = add_flow_node(
        svg, dio, "generator", 4500, lower_y + 190, secondary_w, node_h,
        "Agent", "barista o software generador", "physical",
    )
    svg.line(3660 + secondary_w / 2, lower_y + node_h, 3660 + secondary_w / 2, lower_y + 190, "#64748B", 2.5, True)
    add_svg_edge_label(svg, 3660 + secondary_w / 2, lower_y + 158, "hasKnowledgeBasis", 14)
    dio.edge("hasKnowledgeBasis", sec_ids["recommended"], guidance)
    svg.path(
        f"M {3660+secondary_w} {lower_y+node_h/2} C {4420} {lower_y+node_h/2}, "
        f"{4420} {lower_y+243}, {4500} {lower_y+243}",
        "#64748B", 2.5,
    )
    add_svg_edge_label(svg, 4400, lower_y + 145, "generatedBy", 14)
    dio.edge("generatedBy", sec_ids["recommended"], generator)

    # ParameterAdjustment → RecommendedAction, enrutada entre el eje y las bases de conocimiento.
    svg.path(
        f"M {5340+secondary_w/2} {lower_y+node_h} C {5340+secondary_w/2} {lower_y+170}, "
        f"{3660+secondary_w/2} {lower_y+170}, {3660+secondary_w/2} {lower_y+node_h}",
        "#64748B", 2.5,
    )
    add_svg_edge_label(svg, 4810, lower_y + 164, "basedOnRecommendation", 14)
    dio.edge("basedOnRecommendation", sec_ids["adjustment"], sec_ids["recommended"])

    # AdjustmentOutcomeAssessment → ParameterAdjustment
    svg.line(4500 + secondary_w, lower_y + 38, 5340, lower_y + 38, "#64748B", 2.5, True)
    add_svg_edge_label(svg, 5245, lower_y + 21, "assessesAdjustment", 14)
    dio.edge("assessesAdjustment", sec_ids["outcome"], sec_ids["adjustment"])

    # EspressoExtraction → ParameterAdjustment: intención explícita de evaluar.
    svg.path(
        f"M {ex_mid} {ex_bottom} C {ex_mid+650} {lower_y-125}, "
        f"{5340+secondary_w/2} {lower_y-125}, {5340+secondary_w/2} {lower_y}",
        "#64748B", 2.5,
    )
    add_svg_edge_label(svg, 4960, lower_y - 108, "evaluatesOutcomeOf", 14)
    dio.edge("evaluatesOutcomeOf", nodes["extraction"], sec_ids["adjustment"])

    # EspressoCalibration → ParameterAdjustment
    svg.line(6180, lower_y + 88, 5340 + secondary_w, lower_y + 88, "#64748B", 2.5, True)
    add_svg_edge_label(svg, 6085, lower_y + 72, "hasParameterAdjustment", 14)
    dio.edge("hasParameterAdjustment", sec_ids["calibration"], sec_ids["adjustment"])

    # Leyenda inferior
    svg.text(
        130, 950,
        "Lectura: las clases superiores forman el eje extracción → evidencia → conocimiento; "
        "las ramas representan contexto, conformidad, aceptación, recomendación, ajuste y evaluación posterior.",
        18, "#64748B", 400,
    )
    svg.text(WIDTH - 130, 950, "Fuente: espresso-dt.ttl", 18, "#64748B", 500, "end")
    dio.vertex(
        "Lectura: las clases superiores forman el eje extracción → evidencia → conocimiento; "
        "las ramas representan contexto, conformidad, aceptación, recomendación, ajuste y evaluación posterior.",
        130, 925, WIDTH - 260, 42,
        "text;html=1;align=left;fontSize=16;fontColor=#64748B;strokeColor=none;fillColor=none;",
    )

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    svg_path = BASE.with_suffix(".svg")
    png_path = BASE.with_suffix(".png")
    drawio_path = BASE.with_suffix(".drawio")
    svg_path.write_text(svg.close(), encoding="utf-8")
    drawio_path.write_text(dio.xml(), encoding="utf-8")

    import subprocess

    subprocess.run(
        ["magick", "-background", "white", str(svg_path), str(png_path)],
        check=True,
    )
    print(svg_path)
    print(png_path)
    print(drawio_path)


if __name__ == "__main__":
    build()
