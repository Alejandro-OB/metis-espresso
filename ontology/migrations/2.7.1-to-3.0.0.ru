PREFIX edt: <https://w3id.org/espresso-dt#>
PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#>

# Migra el tipo y los metadatos propios de cada individuo de conocimiento.
DELETE {
  ?old ?predicate ?object .
}
INSERT {
  ?new ?predicate ?object .
  ?new rdf:type ?newType .
}
WHERE {
  VALUES (?old ?new ?newType) {
    (edt:RuleProfileDeviation edt:ProfileConformanceCriterion edt:EvaluationCriterion)
    (edt:RuleBaristaAcidityAdjustment edt:AcidityAdjustmentHeuristic edt:ExpertHeuristic)
    (edt:RuleBaristaCoffeeBedReview edt:CoffeeBedInspectionProcedure edt:DiagnosticProcedure)
    (edt:RuleBaristaIterativeCalibration edt:IterativeCalibrationProcedure edt:CalibrationProcedure)
    (edt:RuleBaristaContextReview edt:EnvironmentalContextReviewProcedure edt:DiagnosticProcedure)
    (edt:RuleHumidityCoffeeBedEffect edt:HumidityCoffeeBedEvidencePattern edt:ContextualEvidencePattern)
    (edt:RuleUnderExtractionAcidity edt:UnderExtractionAcidityHeuristic edt:DiagnosticHeuristic)
    (edt:RuleBaristaHumanSupervision edt:HumanRecommendationValidationPolicy edt:ValidationPolicy)
  }
  ?old ?predicate ?object .
  FILTER (!(?predicate = rdf:type && ?object = edt:Rule))
};

# La comparación frente al perfil pasa a ser un criterio de evaluación.
DELETE {
  ?subject edt:generatedByRule edt:RuleProfileDeviation .
}
INSERT {
  ?subject edt:basedOnCriterion edt:ProfileConformanceCriterion .
}
WHERE {
  ?subject edt:generatedByRule edt:RuleProfileDeviation .
};

# Los antiguos objetos de recommendedByRule se distribuyen según su significado.
DELETE { ?subject edt:recommendedByRule ?old . }
INSERT { ?subject ?newProperty ?new . }
WHERE {
  VALUES (?old ?newProperty ?new) {
    (edt:RuleProfileDeviation edt:basedOnCriterion edt:ProfileConformanceCriterion)
    (edt:RuleBaristaAcidityAdjustment edt:supportedByHeuristic edt:AcidityAdjustmentHeuristic)
    (edt:RuleBaristaCoffeeBedReview edt:followsProcedure edt:CoffeeBedInspectionProcedure)
    (edt:RuleBaristaIterativeCalibration edt:followsProcedure edt:IterativeCalibrationProcedure)
    (edt:RuleBaristaContextReview edt:followsProcedure edt:EnvironmentalContextReviewProcedure)
    (edt:RuleHumidityCoffeeBedEffect edt:supportedByEvidencePattern edt:HumidityCoffeeBedEvidencePattern)
    (edt:RuleUnderExtractionAcidity edt:supportedByHeuristic edt:UnderExtractionAcidityHeuristic)
    (edt:RuleBaristaHumanSupervision edt:governedByPolicy edt:HumanRecommendationValidationPolicy)
  }
  ?subject edt:recommendedByRule ?old .
};

# Los demás usos de generatedByRule también se clasifican explícitamente.
DELETE { ?subject edt:generatedByRule ?old . }
INSERT { ?subject ?newProperty ?new . }
WHERE {
  VALUES (?old ?newProperty ?new) {
    (edt:RuleBaristaAcidityAdjustment edt:supportedByHeuristic edt:AcidityAdjustmentHeuristic)
    (edt:RuleBaristaCoffeeBedReview edt:followsProcedure edt:CoffeeBedInspectionProcedure)
    (edt:RuleBaristaIterativeCalibration edt:followsProcedure edt:IterativeCalibrationProcedure)
    (edt:RuleBaristaContextReview edt:followsProcedure edt:EnvironmentalContextReviewProcedure)
    (edt:RuleHumidityCoffeeBedEffect edt:supportedByEvidencePattern edt:HumidityCoffeeBedEvidencePattern)
    (edt:RuleUnderExtractionAcidity edt:supportedByHeuristic edt:UnderExtractionAcidityHeuristic)
    (edt:RuleBaristaHumanSupervision edt:governedByPolicy edt:HumanRecommendationValidationPolicy)
  }
  ?subject edt:generatedByRule ?old .
};

# Reescribe referencias restantes, por ejemplo assertsKnowledge.
DELETE { ?subject ?predicate ?old . }
INSERT { ?subject ?predicate ?new . }
WHERE {
  VALUES (?old ?new) {
    (edt:RuleProfileDeviation edt:ProfileConformanceCriterion)
    (edt:RuleBaristaAcidityAdjustment edt:AcidityAdjustmentHeuristic)
    (edt:RuleBaristaCoffeeBedReview edt:CoffeeBedInspectionProcedure)
    (edt:RuleBaristaIterativeCalibration edt:IterativeCalibrationProcedure)
    (edt:RuleBaristaContextReview edt:EnvironmentalContextReviewProcedure)
    (edt:RuleHumidityCoffeeBedEffect edt:HumidityCoffeeBedEvidencePattern)
    (edt:RuleUnderExtractionAcidity edt:UnderExtractionAcidityHeuristic)
    (edt:RuleBaristaHumanSupervision edt:HumanRecommendationValidationPolicy)
  }
  ?subject ?predicate ?old .
};

# Elimina declaraciones de tipo antiguas que no contengan otros metadatos.
DELETE { ?old rdf:type edt:Rule . }
WHERE {
  VALUES ?old {
    edt:RuleProfileDeviation
    edt:RuleBaristaAcidityAdjustment
    edt:RuleBaristaCoffeeBedReview
    edt:RuleBaristaIterativeCalibration
    edt:RuleBaristaContextReview
    edt:RuleHumidityCoffeeBedEffect
    edt:RuleUnderExtractionAcidity
    edt:RuleBaristaHumanSupervision
  }
  ?old rdf:type edt:Rule .
}

