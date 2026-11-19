import type { components } from './schema'

export type CoffeeCreateContract = components['schemas']['CoffeeCreate']
export type ManualMeasurementsContract = components['schemas']['ManualMeasurements']

export type Role = 'viewer' | 'barista' | 'admin'
export interface SessionUser {
  id: string
  username: string
  displayName: string
  role: 'barista' | 'admin'
}
export interface AdminUser extends SessionUser {
  active: boolean
}
export type ServiceState = 'available' | 'degraded' | 'unavailable'

export interface Page<T> {
  items: T[]
  page: number
  pageSize: number
  total: number
  pages: number
}

export type SimulationStatus = 'queued' | 'running' | 'succeeded' | 'failed' | 'cancelled'
export type SemanticStatus = 'pending' | 'validating' | 'published' | 'rejected'

export interface SimulationModel {
  id: string
  version: string
  displayName: string
  capabilities: {
    communicationStepSeconds: number
    channels: Array<{ id: string; quantity: string; unit: string; origin: 'simulated' }>
  }
}

export interface SimulationMetric {
  unit: string
  initial: number
  final: number
  minimum: number
  maximum: number
  target: number
  tolerance: number | null
  meanAbsoluteError: number
  withinTolerancePercent: number | null
}

export interface SimulationRun {
  schemaVersion: '1.0'
  simulationRunId: string
  status: SimulationStatus
  semanticStatus: SemanticStatus
  progress: { completedSteps: number; totalSteps: number; percent: number }
  requestedBy: string
  createdAt: string
  startedAt: string | null
  finishedAt: string | null
  model: { id: string; version: string; sha256: string; contractVersion: string; channels?: Array<{ id: string; quantity: string; unit: string; maturity?: string }> }
  profileSnapshot: {
    profileId: string
    profileVersion: number
    targets: { temperatureC: number; pressureBar: number }
    tolerances: { temperatureC: number; pressureBar: number }
  }
  controller: { id: string; version: string; configurationSha256: string }
  durationSeconds: number
  summary: null | { metrics: Record<'temperatureC' | 'pressureBar', SimulationMetric> }
  artifact: null | { sha256: string; sampleCount: number; dataReference: string }
  cancelRequested: boolean
  error: null | { code: string; message: string; retryable: boolean }
}

export interface SimulationSample {
  sequence: number
  simulationTimeSeconds: number
  channels: { temperatureC: number; pressureBar: number; [key: string]: number }
  commands: { heaterCommand: number; pumpCommand: number }
  quality: 'simulated'
}

export interface SimulationSeries {
  manifest: NonNullable<SimulationRun['artifact']>
  samples: SimulationSample[]
}

export interface SimulationSemantic {
  simulationRunId: string
  status: SemanticStatus
  publication: null | {
    namedGraphIri: string
    resourceIri: string
    publishedAt: string | null
    validation: { conforms: boolean; shapesVersion: string; validatedAt: string }
  }
  document: null | { '@id': string; graphIri: string; '@graph': unknown[] }
}

export interface Extraction {
  id: string
  source: 'measured' | 'simulation'
  deviceId?: string
  /** Con qué se midió (RF-014); ausente o null si el dispositivo no lo declaró. */
  calibration?: { id: string; version: number } | null
  firmwareVersion?: string | null
  /** Subida de caldera del primer uso que la placa aplicó en esta preparación (ADR-018 § 12). */
  boilerOffsetAppliedC?: number | null
  profile: {
    id: string
    version: number
    status?: string
    targets?: Record<string, string | number | boolean | null>
    tolerances?: Record<string, string | number | boolean | null>
    [key: string]: unknown
  }
  coffeeId?: string | null
  coffeeReference?: string | null
  coffeeSnapshot?: Coffee | null
  machineId?: string | null
  grinderId?: string | null
  grindSetting?: number | null
  notes?: string | null
  calibrationId?: string | null
  environment?: { temperatureC?: number | null; humidityPercent?: number | null }
  /** De qué sesión de catación es (protocolo de catación § 3–§ 4); ausente si no es de ninguna. */
  tastingSession?: { sessionCode?: string; blindCode?: string; block?: 'A' | 'B' | 'C' | 'D' | 'E'; condition?: string } | null
  portafilterConfiguration?: 'pressurized-double-coffee' | 'non-pressurized-double-coffee' | 'no-basket-water' | null
  targetSensoryProfile?: {
    id: string
    definedBy: string
    rationale: string
    descriptors: string[]
  } | null
  barista?: string | null
  // Nulos solo en una preparación cortada por falta de energía: el dispositivo
  // no vio el final y, sin hora de pared, tampoco el inicio (ADR-005 § 8.10).
  startedAt: string | null
  finishedAt: string | null
  durationMs: number | null
  /** Cuándo se supo de ella; en una preparación cortada, el rearranque que la cerró. */
  ingestedAt?: string | null
  termination: string
  terminationDetail?: string | null
  sampleCount: number
  seriesRef?: string | null
  verificationForEpisodeId?: string
  statistics: ExtractionStatistics
  manualMeasurements: {
    coffeeDoseG?: number
    beverageMassG?: number
    tdsPercent?: number
    /** Lecturas del refractómetro de las que sale `tdsPercent`, si se registraron. */
    tdsReadings?: number[]
    extractionYieldPercent?: number
    instrument?: string | null
    recordedBy?: string
    recordedAt?: string
    entries?: Record<string, { value: string | number; unit?: string | null; recordedBy: string; recordedAt: string; readings?: number[] }>
    completion: {
      missingFields: Array<{ field: string; label: string }>
      results: { extractionYieldPercent: { status: 'available' | 'pending'; missingFields: string[] } }
    }
  }
  sensoryEvaluation?: SensoryEvaluation
  provenance?: Provenance
  revisions?: RecordRevision[]
  history?: {
    barista?: string | null
    coffeeId?: string | null
    variety?: string | null
    process?: string | null
    roast?: string | null
    profileId?: string | null
    profileVersion?: number | null
    machineId?: string | null
    grinderId?: string | null
    calibrationId?: string | null
    technicalConformance: 'conformant' | 'non-conformant' | 'insufficient-information'
    sensoryAcceptance: 'accepted' | 'rejected' | 'inconclusive' | 'not-recorded'
    defect: string[]
    termination: string
    source: 'measured' | 'simulation'
  }
}

export interface ExtractionHistoryRecord {
  extraction: Extraction
  coffee?: Coffee | null
  result: ExtractionResult
  series: { extractionId: string; seriesRef: string; samples: LiveSample[] }
  recommendations: Recommendation[]
  episodes: Episode[]
  calibrations: Calibration[]
  relatedExtractionIds: string[]
}

export type TwinVerdict =
  | 'within-model-tolerance'
  | 'outside-model-tolerance'
  | 'insufficient-coverage'
  | 'no-tolerance-declared'
  | 'not-evaluable'

export interface TwinChannelDifference {
  channel: 'temperatureC' | 'pressureBar'
  unit: string
  comparedSampleCount: number
  coverageRatio: number
  meanSignedDifference: number | null
  meanAbsoluteDifference: number | null
  rootMeanSquareDifference: number | null
  maxAbsoluteDifference: number | null
  elapsedMsAtMaxDifference: number | null
  modelTolerance: number | null
  verdict: TwinVerdict
  contributesToVerdict: boolean
  modelLimitation?: string
}

export interface TwinComparison {
  schemaVersion: string
  comparisonId: string
  extractionId: string
  simulationRunId: string
  status?: 'succeeded' | 'failed'
  error?: { code: string; message: string } | null
  alignment?: {
    method: string
    measuredSampleCount: number
    comparedSampleCount: number
    coverageRatio: number
    minimumCoverageRatio: number
    excluded: { invalidQuality: number; outsideSimulatedRange: number }
    initialConditions: Record<string, number>
  }
  channels?: TwinChannelDifference[]
  verdict?: TwinVerdict
  model?: { id: string; version: string; sha256: string }
  provenance?: { actor: string; recordedAt: string; source: string }
  createdAt: string
}

export interface TwinComparisonList {
  extractionId: string
  latest: TwinComparison | null
  items: TwinComparison[]
  total: number
}

// Agua sobre el café que estima el gemelo con el reposo y los usos seguidos.
export interface TwinWater {
  method: string
  status: 'estimated' | 'no-data' | 'no-ambient'
  estimatedC: number | null
  useIndex: number | null
  restS: number | null
  previousEstimatedC: number | null
  differenceC: number | null
  within: boolean | null
  assumptions: Array<Record<string, unknown>>
}

export interface MachineStability {
  rule: string
  thermal: { bandC: number; referenceC: number | null; status: 'measured' | 'no-reference' | 'no-wait-samples'; fraction: number | null; sampleCount: number; waitS: number | null; coverage: number | null; maxDeviationC: number | null }
  pressure: { bandBar: number; referenceBar: number | null; status: 'measured' | 'no-reference' | 'never-reached-band'; fraction: number | null; startMs: number | null; averageCount: number }
  water?: { bandC: number; estimatedC: number | null; previousEstimatedC: number | null; differenceC: number | null; within: boolean | null; method: string | null; status: 'estimated' | 'no-previous' | 'outside-line-range' | 'no-body-at-press' | 'no-line' | 'line-changed'; previousExtractionId?: string; twin?: TwinWater }
}

export interface ExtractionResult {
  extractionId: string
  digitalTwin: TwinComparison | null
  /** Estabilidad de la máquina (monografía § 4.4): bandas medidas, no la tolerancia del perfil. */
  machineStability?: MachineStability
  /** La evaluación guardada que se muestra; `recorded` es falso en preparaciones anteriores a que se guardaran. */
  assessment?: {
    id: string | null
    sequence: number | null
    assessedAt: string | null
    trigger: 'extraction-closed' | 'extraction-synced' | 'series-received' | 'manual-measurements' | null
    rule: string
    window: ExtractionStatistics['window']
    recorded: boolean
  }
  technicalConformance: {
    status: 'conformant' | 'non-conformant' | 'insufficient-information'
    profileId: string
    profileVersion: number
    assessedProperties: number
    totalProperties: number
    missingData: string[]
  }
  sensoryAcceptance: {
    status: 'accepted' | 'rejected' | 'inconclusive' | 'not-recorded'
    rationale?: string | null
  }
  properties: Array<{
    property: string
    label: string
    unit: string
    objective: { target?: number; tolerance?: number | null; minimum: number | null; maximum: number | null }
    observed: { value: number; minimum: number; maximum: number } | null
    // La caldera al pulsar dicha en agua sobre el café (recta del acta del café).
    water?: {
      objective: { target: number | null; tolerance: number | null; minimum: number | null; maximum: number | null }
      estimatedC: number | null
      bound: { side: 'above' | 'below'; limitC: number } | null
      deviationAmountC: number | null
      boilerOffsetC: number
    }
    deviation: {
      status: 'unassessed' | 'within' | 'outside' | 'unavailable' | 'no-tolerance' | 'not-evaluable'
      reason?: string | null
      direction: 'below' | 'above' | 'both' | null
      amount: number | null
      firstOutsideMs?: number | null
      outsideDurationMs?: number | null
    }
    source: 'instrumental' | 'manual' | 'calculated' | 'unavailable'
    evidenceId: string
  }>
  diagnosis: {
    conclusion: string
    explanation: string
    evidenceIds: string[]
    knowledgeSource: { name: string; version: string }
    generatedBy: { name: string; version: string }
    generatedAt: string
  }
  evidence: Array<{
    id: string
    label: string
    value: { value: number; minimum: number; maximum: number }
    source: 'instrumental' | 'manual' | 'calculated'
  }>
  recommendations: Array<{
    recommendationId: string
    status: string
    summary: string
    createdAt: string
  }>
}

export interface LiveSample {
  id: string
  sequence: number
  elapsedMs: number
  occurredAt: string
  temperatureC: number | null
  pressureBar: number | null
  quality: 'valid' | 'missing'
}

export interface LivePreparation {
  active: boolean
  preparationId: string | null
  boilerReferenceC?: number | null
  pressureTargetBar?: number | null
  samples: LiveSample[]
}

export interface LiveExtraction {
  id: string
  status: 'running' | 'closed'
  source: 'simulation'
  sourceLabel: string
  profile: {
    id: string
    name?: string | null
    version: number
    targets: { temperatureC?: number; pressureBar?: number }
    tolerances: { temperatureC?: number; pressureBar?: number }
  }
  coffeeId: string
  coffeeReference?: string | null
  startedAt: string
  lastSampleAt?: string | null
  sampleCount: number
  environment: { temperatureC?: number | null; humidityPercent?: number | null }
  missingIntervals: Array<{ fromMs: number; toMs: number }>
  samples: LiveSample[]
  observations: Array<{
    code: string
    message: string
    observedAt: string
    observedValue?: number
    limit?: number
    unit?: string
  }>
  verificationForEpisodeId?: string
  machineId?: string | null
  grinderId?: string | null
  calibrationId?: string | null
}

export interface ExtractionAlertPreferences {
  temperature: boolean
  pressure: boolean
  acquisition: boolean
  updatedBy?: string
  updatedAt?: string | null
}

export interface Coffee {
  id: string
  commercialReference?: string | null
  lotIdentifier?: string | null
  bagIdentifier?: string | null
  variety?: string | null
  postHarvestProcess?: string | null
  roastLevel?: string | null
  roastDate?: string | null
  region?: string | null
  altitudeM?: number | null
  soilCharacteristics?: string | null
  mineralComposition?: string | null
  mineralSource?: string | null
  notApplicableFields?: CoffeeOptionalField[]
  declaredSensoryProfile?: { id: string; declaredBy: string; source: string; originalText: string; descriptors: string[]; recordedBy?: string; recordedAt?: string } | null
  provenance?: Provenance
}

export interface ProfileSpecification {
  property: string
  unit: string
  strategy: 'target' | 'range'
  target?: number | null
  tolerance?: number | null
  minimum?: number | null
  maximum?: number | null
}

export interface PreparationProfile {
  id: string
  familyId: string
  name: string
  version: number
  status: 'draft' | 'approved' | 'retired' | 'proposed' | 'accepted' | 'effective'
  coffeeIds: string[]
  specifications: ProfileSpecification[]
  /** Ausente en los perfiles anteriores al campo: la placa usa su corte compilado. */
  purgeMaximumTimeMs?: number | null
  sensoryTarget?: SensoryTarget | null
  previousVersionId?: string | null
  createdAt?: string
  approvedAt?: string
  retiredAt?: string
  provenance?: Provenance
  revisions?: RecordRevision[]
}

export interface RecordRevision {
  id: string
  resourceId: string
  category: 'coffee-context' | 'manual-measurements' | 'sensory-evaluation' | 'profile-retired'
  previousRecordId: string
  recordedBy: string
  recordedAt: string
}

export interface ProfileComparisonItem extends PreparationProfile {
  author?: string | null
  applicableCoffees: Array<{ id: string; name: string }>
  validationEvidence: Array<{
    id: string
    result?: string | null
    rationale?: string | null
    finishedAt?: string | null
    testExtractionIds: string[]
  }>
  supportingExtractions: Array<{
    id: string
    finishedAt?: string | null
    coffeeReference?: string | null
  }>
}

export interface ProfileComparison {
  items: ProfileComparisonItem[]
  changes: Array<{
    key: string
    label: string
    unit?: string | null
    values: unknown[]
    status: 'equal' | 'different' | 'missing'
  }>
  changedKeys: string[]
}

export interface TraceabilityRecord {
  start: { kind: 'barista' | 'profile' | 'extraction'; id: string; label: string }
  nodes: Array<{
    id: string
    kind: 'barista' | 'profile' | 'extraction' | 'observation' | 'evaluation' | 'diagnosis' | 'explanation' | 'recommendation' | 'decision' | 'adjustment' | 'result'
    label: string
    summary?: string | null
    detailUrl: string
    occurredAt?: string | null
    relatedTo?: string | null
  }>
}

export interface Equipment {
  id: string
  type: 'machine' | 'grinder'
  name: string
  location: string
  capabilities: Array<{ property: string; manualAdjustment: boolean; automaticAdjustment: boolean }>
  sensors: Array<{ id: string; measuredProperty: string; location: string; accuracy: number; accuracyUnit: string; samplingFrequencyHz: number; lastCalibrationAt: string }>
  provenance?: Provenance
}

export interface DesiredSensoryChange {
  attribute: 'Fragrance' | 'Aroma' | 'Flavor' | 'Acidity' | 'Sweetness' | 'Bitterness' | 'Body' | 'Aftertaste' | 'Astringency' | 'Balance' | 'Mouthfeel'
  direction: 'Increase' | 'Decrease' | 'Improve'
}

export interface SensoryTarget {
  id?: string
  coffeeId: string
  rationale: string
  descriptors: string[]
  otherDescriptors?: string[]
  desiredChanges: DesiredSensoryChange[]
  definedBy?: string
  recordedAt?: string
  inheritedFrom?: string | null
}

export type CoffeeOptionalField = 'commercialReference' | 'lotIdentifier' | 'bagIdentifier' | 'variety' | 'postHarvestProcess' | 'roastLevel' | 'roastDate' | 'region' | 'altitudeM' | 'soilCharacteristics' | 'mineralComposition' | 'mineralSource'

export interface SensoryEvaluation {
  id: string
  evaluatedBy: string
  evaluatedAt: string
  protocolReference?: string | null
  servingTemperatureC?: number | null
  minutesSinceExtraction?: number | null
  descriptors: string[]
  descriptorRatings?: Array<{ descriptor: string; intensity: number }>
  otherDescriptors?: Array<{ name: string; intensity: number }>
  mainTastes?: string[]
  mouthfeelTextures?: string[]
  defects: string[]
  scores: Array<{ attribute: string; dimension: string; value: number }>
  desiredChanges?: Array<{ attribute: string; direction: string }>
  comparisons?: Array<'declared-profile' | 'barista-target'>
  acceptance: { status: 'accepted' | 'rejected' | 'inconclusive'; rationale?: string | null }
  comment?: string | null
}

export interface Episode {
  id: string
  status?: 'awaiting-verification' | 'verification-running' | 'assessed'
  coffeeId: string
  initialExtractionId: string
  followUpExtractionId: string | null
  adjustment: { property: string; previousValue: number; newValue: number; unit: string; rationale?: string; occurredAt?: string; recommendationId?: string | null; performedBy?: string; registeredAt?: string }
  targetOutcome: { kind: string; code: string }
  outcome: null | { category: string; rationale: string; assessedAt: string; assessedBy?: string; comparativeEvidence?: Array<{ property: string; initialEvidenceId: string; followUpEvidenceId: string }> }
  provenance?: Provenance
}

export interface CalibrationAdjustment {
  id: string
  property: string
  previousValue: number
  newValue: number
  unit: string
  rationale: string
  occurredAt: string
  performedBy: string
}

export interface CalibrationEvent {
  sequence: number
  kind: 'started' | 'test-added' | 'adjustment-added' | 'closed'
  occurredAt: string
  recordedBy: string
  extractionId?: string
  adjustmentId?: string
  result?: 'validated-profile' | 'new-profile-version' | 'rejected'
  profileId?: string | null
}

export interface Calibration {
  id: string
  status: 'open' | 'closed'
  coffeeId: string
  profileId?: string | null
  profileName?: string | null
  context: { station?: string | null; machineId?: string | null; grinderId?: string | null; notes?: string | null }
  startedAt: string
  startedBy: string
  testExtractionIds: string[]
  adjustments: CalibrationAdjustment[]
  events: CalibrationEvent[]
  result?: 'validated-profile' | 'new-profile-version' | 'rejected'
  rationale?: string
  resultProfileId?: string | null
  finishedAt?: string
  closedBy?: string
  provenance?: Provenance
}

export interface Recommendation {
  origin?: 'knowledge-base' | 'machine-learning' | 'reasoner'
  model?: null | { name: string; version: string | null; approvalStatus: 'approved' | 'candidate' | 'unavailable' }
  /** Solo cuando la propuesta es del razonador de EKO. */
  diagnosis?: { iri: string | null; summary: string; explanations: string[] }
  /** La regla de la ontología que respalda la propuesta y de dónde sale (EKO 3.4.0). */
  heuristic?: { label: string; description: string; source: string; confidence: string } | null
  alternatives?: string[]
  recommendationId: string
  extractionId: string
  status: 'abstained' | 'test-only' | 'pending-human-review'
  reasonCode: string
  summary: string
  respondsTo: { code: string; summary: string }
  proposedAdjustment: null | { property: string; direction: string; value: number | null; unit: string | null }
  uncertainty: { value: number | null; methodReference: string }
  evidenceIds: string[]
  knowledgeSource: { name: string; version: string }
  applicability: string
  requiresHumanReview: boolean
  actuatorCommand: null
  predictedSensoryProfile?: null | {
    coffeeId: string
    descriptors: Array<{ descriptor: string; label: string; intensity: number }>
    provenance: { source: string; generatedAt: string }
  }
  createdAt: string
  decision?: {
    status: 'accepted' | 'rejected' | 'modified'
    comment?: string | null
    modifiedAdjustment?: Recommendation['proposedAdjustment']
    decidedBy: string
    decidedAt: string
  }
}

export interface Provenance { actor?: string; recordedAt?: string; source: string }

export interface StatusResponse {
  connected: boolean
  latestTelemetry: null | {
    occurredAt: string
    state: string
    source: string
    measurements?: { temperatureC: number; pressureBar: number }
  }
  services: Array<{ id: string; label: string; status: ServiceState; detail?: string; adapter?: string }>
  checkedAt: string
}

export interface DashboardResponse {
  activeProfile: Extraction['profile'] | null
  activeCoffee: Coffee | null
  activeEquipment: { machineId: string | null; grinderId: string | null; calibrationId: string | null }
  sensors: Array<{ id: string; label: string; status: 'available' | 'unavailable'; value: number | null; unit: string }>
  lastReadingAt: string | null
  lastReadingDeviceId?: string | null
  lastCalibration: null | { id: string; finishedAt?: string; result?: string; profileName?: string }
  pendingActions: Array<{ kind: 'coffee' | 'measurements' | 'sensory' | 'calibration'; label: string; extractionId: string }>
  recentExtractions: Extraction[]
}

export interface ApiErrorField { field: string; label?: string; message: string }
export interface ApiErrorShape { error: { code: string; message: string; fields: ApiErrorField[]; correlationId: string } }

/**
 * Indicadores de estabilidad. Van en null cuando no hay con qué calcularlos: la
 * presión nunca llegó a su rango o el perfil no declara tolerancia. `window` y
 * `temperatureSite` faltan en los registros anteriores a la regla, que cubrían
 * la preparación entera.
 */
export interface ExtractionStatistics {
  temperatureMaeC: number | null
  pressureMaeBar: number | null
  temperatureWithinTolerancePercent: number | null
  pressureWithinTolerancePercent: number | null
  window?: { rule: string; status: 'undetermined' | 'stabilized' | 'never-stabilized' | 'no-pressure-range'; startMs: number | null }
  temperatureSite?: 'boiler' | 'water'
}
