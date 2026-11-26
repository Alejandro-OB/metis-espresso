import { useEffect, useRef, useState } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { useParams, useSearchParams } from 'react-router-dom'
import { api, resilientPut } from '@/shared/api/client'
import type { Coffee, Extraction, ExtractionHistoryRecord, ExtractionResult, PreparationProfile } from '@/shared/api/types'
import { Alert, Breadcrumbs, Button, Checkbox, Field, Input, LinkButton, LoadingState, NumberField, ProvenanceBadge, Surface, TextArea, useToast } from '@/shared/ui'
import { duration, formatDateTime, formatNumber } from '@/shared/lib/format'
import { downloadAttachment } from '@/shared/lib/download'
import { ContextFields } from './context-editor'
import { contextFromExtraction, contextPayload } from './preparation-context'
import { PageHeading } from './page-heading'
import { LiveChart } from './live-chart'
import { seriesComplete } from './series-state'
import { waterVsPrevious } from './water-vs-previous'
import { MoreActions } from './more-actions'
import { NextCoffee } from './next-coffee'
import { sensoryAttributeLabel, sensoryAttributes, sensoryDefectLabel, sensoryDefects, sensoryDescriptorGroups, sensoryDescriptorLabel, sensoryDirectionLabel, basicTastes, mouthfeelTextures, sensoryProtocolReference } from './sensory-options'
import { TwinComparisonPanel } from './twin-comparison'
import { InterruptedPreparationNotice } from './interrupted-preparation'
import { interruptedByPowerLoss, preparationMomentLabel } from './preparation-status'
import styles from './routes.module.css'

type ManualMeasurements = Extraction['manualMeasurements']
type ManualMeasurementsPayload = Partial<Pick<ManualMeasurements, 'coffeeDoseG' | 'beverageMassG' | 'tdsPercent' | 'tdsReadings' | 'instrument'>>


export default function ExtractionDetailRoute() {
  const { id = '' } = useParams()
  return <ExtractionWorkspace id={id} withBreadcrumbs/>
}

// El trabajo sobre una preparación. Lo usan el detalle del historial y
// «Preparar café», que lo muestra en la misma página en cuanto la placa entrega
// el café recién hecho.
//
// Una sola página de arriba abajo, en el orden en que el barista la usa
// (23-oct-2026): qué salió, cómo quedó frente al perfil, la cata, qué hacer
// después y los datos. Antes eran tres pestañas, tres botones de descarga y
// once tarjetas del mismo peso. Lo que no usa para decidir (desempeño
// estadístico, gemelo, sostenimiento, historial de cambios, evidencia
// semántica) va plegado al final, no fuera: la tesis lo necesita a mano.
// «Contexto conservado» se quitó porque repetía los datos de la preparación.
// Lo que se sigue esperando la serie de un café recién terminado.
const SERIES_WAIT_MS = 3 * 60_000

export function ExtractionWorkspace({ id, withBreadcrumbs = false, single = false }: { id: string; withBreadcrumbs?: boolean; single?: boolean }) {
  const [searchParams] = useSearchParams()
  const [correctingSensory, setCorrectingSensory] = useState(false)
  const queryClient = useQueryClient()
  const toast = useToast()
  const extraction = useQuery({ queryKey: ['extraction', id], queryFn: () => api<Extraction>(`/api/v1/extractions/${id}`), enabled: Boolean(id) })
  // La serie llega en tramos después del resumen del café (26-oct-2026): un
  // café medido que terminó hace poco y todavía no trae lecturas se vuelve a
  // pedir cada 3 s, con su resultado, hasta que llegan. Antes la página se
  // quedaba con el primer vistazo, sin temperatura, presión ni curvas, hasta
  // que alguien la recargaba.
  const seriesPending = (record: ExtractionHistoryRecord | undefined) => Boolean(record && record.extraction.source === 'measured' && !seriesComplete(record) && record.extraction.finishedAt && Date.now() - Date.parse(record.extraction.finishedAt) < SERIES_WAIT_MS)
  const historyRecord = useQuery({ queryKey: ['extraction-history-record', id], queryFn: () => api<ExtractionHistoryRecord>(`/api/v1/extractions/${id}/history-record`), enabled: Boolean(id), refetchInterval: (query) => seriesPending(query.state.data) ? 3_000 : false })
  const waitingSeries = seriesPending(historyRecord.data)
  const result = useQuery({ queryKey: ['extraction-result', id], queryFn: () => api<ExtractionResult>(`/api/v1/extractions/${id}/result`), enabled: Boolean(id), refetchInterval: waitingSeries ? 3_000 : false })
  // Una última vez al llegar la serie: el resultado que se pidió mientras
  // esperaba puede ser de antes de que el servidor la juzgara.
  const wasWaiting = useRef(false)
  useEffect(() => {
    if (wasWaiting.current && !waitingSeries) void queryClient.invalidateQueries({ queryKey: ['extraction-result', id] })
    wasWaiting.current = waitingSeries
  }, [waitingSeries, id, queryClient])
  const referenceCoffee = useQuery({ queryKey: ['coffee', extraction.data?.coffeeId, 'sensory-reference'], queryFn: () => api<Coffee>(`/api/v1/coffees/${extraction.data!.coffeeId}`), enabled: Boolean(extraction.data?.coffeeId) })
  const referenceProfile = useQuery({ queryKey: ['profile', extraction.data?.profile.id, 'sensory-reference'], queryFn: () => api<PreparationProfile>(`/api/v1/profiles/${extraction.data!.profile.id}`), enabled: Boolean(extraction.data?.profile.id) })
  const sensory = useMutation({
    mutationFn: (payload: Record<string, unknown>) => resilientPut<Extraction>(`/api/v1/extractions/${id}/sensory-evaluation`, payload),
    onSuccess: async () => { setCorrectingSensory(false); localStorage.removeItem(`espresso:sensory-draft:${id}:v2`); await Promise.all([queryClient.invalidateQueries({ queryKey: ['extraction', id] }), queryClient.invalidateQueries({ queryKey: ['extraction-result', id] }), queryClient.invalidateQueries({ queryKey: ['extraction-history-record', id] })]); toast.push('Evaluación registrada', 'La observación quedó vinculada a la preparación.', 'success') },
  })
  if (extraction.isLoading) return <LoadingState label="Cargando detalle" />
  if (extraction.error || !extraction.data) return <Alert variant="danger" title="No se encontró la extracción">{extraction.error?.message ?? 'No hay datos.'}</Alert>
  const item = extraction.data
  const interrupted = interruptedByPowerLoss(item)
  const moment = preparationMomentLabel(item)
  return <div className={styles.page}>
    {withBreadcrumbs ? <Breadcrumbs items={[{ label: 'Historial', to: '/extractions' }, { label: moment }]}/> : null}
    <div className={styles.profileHeader}>
      {/* En Preparar café el encabezado es «Resumen» (27-oct-2026, pedido del
          usuario): el nombre del café como título parecía otra sección. El
          café pasa a la línea de datos. */}
      <PageHeading title={single ? 'Resumen' : item.coffeeReference ?? 'Preparación sin café registrado'} detail={single ? [item.coffeeReference, moment, `Perfil v${item.profile.version}`].filter(Boolean).join(' · ') : `${moment} · Perfil v${item.profile.version}`} />
      <div className={styles.profileHeaderActions}>
        {item.source === 'simulation' ? <ProvenanceBadge source={item.source}/> : null}
        <MoreActions>
          <LinkButton to={`/traceability/extraction/${item.id}`} variant="quiet">Ver trazabilidad</LinkButton>
          <Button variant="quiet" onPress={() => downloadAttachment(`/api/v1/extractions/${item.id}/export`)}>Descargar ficha</Button>
          <Button variant="quiet" onPress={() => downloadAttachment(`/api/v1/extractions/${item.id}/semantic`)}>Descargar datos enlazados</Button>
        </MoreActions>
      </div>
    </div>
    {interrupted ? <InterruptedPreparationNotice extraction={item}/> : null}
    {searchParams.get('completed') === '1' && item.manualMeasurements.completion.missingFields.length ? <Alert variant="info" title="Completa los datos de cierre">Puedes guardarlos por separado y regresar más tarde.</Alert> : null}
    <Surface>{single ? null : <h2 className={styles.surfaceTitle}>Resumen</h2>}<CupSummary item={item} result={result.data}/></Surface>
    <Surface><h2 className={styles.surfaceTitle}>Frente al perfil</h2>{result.isLoading ? <LoadingState label="Evaluando resultado" /> : null}{result.error ? <Alert variant="danger" title="No se pudo evaluar el resultado">{result.error.message}</Alert> : null}{result.data ? <ExtractionResultPanel result={result.data} /> : null}</Surface>
    <Surface><h2 className={styles.surfaceTitle}>Cata</h2>
      {item.sensoryEvaluation && !correctingSensory
        ? <><SensoryEvaluationSummary evaluation={item.sensoryEvaluation}/><div className={styles.recipeActions}><Button variant="secondary" onPress={() => setCorrectingSensory(true)}>Editar cata</Button></div>
          {/* El paso siguiente a la cata, en el mismo recuadro (23-oct-2026):
              el barista evalúa la taza y ahí mismo decide qué cambia para el
              próximo café, sin buscar otra sección. */}
          <div className={styles.nextCoffeeStep}><NextCoffee item={item} record={historyRecord.data}/></div></>
        : <><details className={styles.profileAdvanced}><summary>Ver lo que dicen el tostador y el barista</summary><SensoryReferences coffee={referenceCoffee.data} profile={referenceProfile.data}/></details><SensoryForm draftKey={`espresso:sensory-draft:${id}`} initial={item.sensoryEvaluation} hasDeclaredReference={Boolean(referenceCoffee.data?.declaredSensoryProfile)} hasTargetReference={Boolean(referenceProfile.data?.sensoryTarget)} pending={sensory.isPending} error={sensory.error} onSubmit={(payload) => sensory.mutate(payload)} onCancel={item.sensoryEvaluation ? () => setCorrectingSensory(false) : undefined}/></>}
    </Surface>
    <Surface><h2 className={styles.surfaceTitle}>Datos de la preparación</h2><PreparationData key={`${item.id}-${item.revisions?.length ?? 0}`} item={item}/></Surface>
    <PreparationCurves record={historyRecord.data} loading={historyRecord.isLoading} error={historyRecord.error}/>
    {/* Solo el gemelo y la conclusión (23-oct-2026): el desempeño estadístico,
        la estabilidad, el sostenimiento, el historial de cambios y la
        evidencia semántica eran demasiado para el barista. Siguen guardados
        y salen en las descargas de «Más». */}
    <details className={styles.profileAdvanced}><summary>Detalles técnicos</summary><div className={styles.stack}>
      {interrupted ? null : <TwinComparisonPanel extractionId={id} comparison={result.data?.digitalTwin ?? null}/>}
      {result.data ? <Surface><h2 className={styles.surfaceTitle}>Diagnóstico</h2><p>{result.data.diagnosis.conclusion}</p></Surface> : null}
    </div></details>
  </div>
}

// Lo que el resultado dice que salió para una propiedad, con su unidad; es el
// mismo valor de la tabla «Frente al perfil».
function observedText(result: ExtractionResult | undefined, properties: string[], digits: number): string {
  const property = result?.properties.find((item) => properties.includes(item.property))
  return property?.observed ? `${formatNumber(property.observed.value, digits)} ${property.unit}` : '—'
}

// El agua sobre el café no la mide ningún sensor: la estima el gemelo con el
// metal de la caldera, el reposo y el cuarto (26-oct-2026). El recuadro
// buscaba la propiedad `water-temperature` del resultado, que los perfiles
// juzgados contra la caldera ya no traen, y salía siempre vacío. Se muestra la
// estimación del gemelo, o la de la recta del acta v6 si el gemelo no pudo, y
// siempre dice que es estimada; un perfil antiguo que juzgaba el agua sigue
// mostrando lo observado.
//
// Desde el 26-oct-2026 manda la de la recta, la misma que «Frente al perfil»:
// sale de mediciones y es la que usa la máquina para regular. El gemelo no
// está comprobado con la subida del primer uso (ese día daba 94,2 °C frente a
// 92,1 °C) y queda solo como respaldo cuando no hay caldera al pulsar.
type ResultProperty = ExtractionResult['properties'][number]

function boundText(bound: { side: 'above' | 'below'; limitC: number }) {
  return `${bound.side === 'above' ? 'más de' : 'menos de'} ${formatNumber(bound.limitC, 1)} °C`
}

function waterAtPress(result: ExtractionResult | undefined) {
  return result?.properties.find((item) => item.water)?.water
}

function waterEstimate(result: ExtractionResult | undefined): number | null {
  const water = result?.machineStability?.water
  return waterAtPress(result)?.estimatedC ?? water?.twin?.estimatedC ?? water?.estimatedC ?? null
}

function waterText(result: ExtractionResult | undefined): string {
  const estimate = waterEstimate(result)
  const bound = waterAtPress(result)?.bound
  return estimate != null ? `${formatNumber(estimate, 1)} °C` : bound ? boundText(bound) : observedText(result, ['water-temperature'], 1)
}

function waterNote(result: ExtractionResult | undefined): string {
  return waterEstimate(result) != null || waterAtPress(result)?.bound ? 'estimada' : ''
}

// Lo primero que se mira: tiempo, agua, presión, dosis, taza y TDS en grande, y los dos
// veredictos en una frase. El primero dice qué falló, no solo que falló.
function CupSummary({ item, result }: { item: Extraction; result: ExtractionResult | undefined }) {
  const manual = item.manualMeasurements
  const outside = result?.properties.find((property) => property.deviation.status === 'outside')
  const failed = outside ? asShown(outside) : undefined
  const technical = !result ? 'Evaluando…'
    : result.technicalConformance.status === 'conformant' ? 'Sí'
      : result.technicalConformance.status === 'non-conformant' ? `No${failed ? `: ${failed.label.toLocaleLowerCase()}, ${deviationLabel(failed).split(';')[0]}` : ''}`
        : 'Sin datos suficientes'
  const sensoryStatus = result?.sensoryAcceptance.status ?? 'not-recorded'
  const sensoryText = `${sensoryAcceptanceLabels[sensoryStatus]}${result?.sensoryAcceptance.rationale ? `: ${result.sensoryAcceptance.rationale}` : ''}`
  return <div className={styles.cupSummary}>
    <div className={styles.profileValues}>
      <div><span>Tiempo</span><strong>{duration(item.durationMs)}</strong></div>
      <div><span>Temperatura media del agua</span><strong>{waterText(result)}</strong>{waterNote(result) ? <small className={styles.meta}>{waterNote(result)}</small> : null}{waterVsPrevious(result) ? <small className={styles.meta}>{waterVsPrevious(result)}</small> : null}</div>
      <div><span>Presión</span><strong>{observedText(result, ['extraction-pressure', 'pressure'], 1)}</strong></div>
      <div><span>Dosis</span><strong>{manual.coffeeDoseG != null ? `${formatNumber(manual.coffeeDoseG)} g` : '—'}</strong></div>
      <div><span>Taza</span><strong>{manual.beverageMassG != null ? `${formatNumber(manual.beverageMassG)} g` : '—'}</strong></div>
      <div><span>TDS</span><strong>{manual.tdsPercent != null ? `${formatNumber(manual.tdsPercent, 2)} %` : '—'}</strong></div>
    </div>
    {manual.extractionYieldPercent != null ? <p className={styles.meta}>Rendimiento de extracción {formatNumber(manual.extractionYieldPercent, 1)} %</p> : null}
    <div className={styles.resultDecisions}>
      <section className={styles.resultDecision}><span>¿Cumplió el perfil?</span><strong>{technical}</strong></section>
      <section className={styles.resultDecision}><span>Cata</span><strong>{sensoryText}</strong></section>
    </div>
  </div>
}

// Los datos y las medidas del café en una sola lista, con un solo «Corregir»
// (23-oct-2026). Antes eran dos listas con dos botones, cifras grandes que ya
// estaban en el Resumen y una línea «Registrado por…» en cada medida; quién
// registró qué sigue guardado y sale en la ficha descargada.
const BASKETS: Record<string, string> = { 'pressurized-double-coffee': 'Canastilla de dos tazas presurizada', 'non-pressurized-double-coffee': 'Canastilla de dos tazas no presurizada', 'no-basket-water': 'Sin canastilla' }

function PreparationData({ item }: { item: Extraction }) {
  const [editing, setEditing] = useState(false)
  const manual = item.manualMeasurements
  const none = 'Sin registrar'
  const readings = manual.tdsReadings?.length ? ` · lecturas ${manual.tdsReadings.map((reading) => formatNumber(reading, 2)).join(' / ')}` : ''
  const rows = [
    ['Café', item.coffeeReference ?? none],
    ['Dosis', manual.coffeeDoseG != null ? `${formatNumber(manual.coffeeDoseG)} g` : none],
    ['Masa en taza', manual.beverageMassG != null ? `${formatNumber(manual.beverageMassG)} g` : none],
    ['TDS', manual.tdsPercent != null ? `${formatNumber(manual.tdsPercent, 2)} %${readings}` : none],
    ['Rendimiento', manual.extractionYieldPercent != null ? `${formatNumber(manual.extractionYieldPercent, 1)} %` : 'Falta dosis, taza o TDS'],
    ['Molienda', item.grindSetting != null ? `${item.grindSetting} clics` : none],
    ['Tipo de canastilla', item.portafilterConfiguration ? BASKETS[item.portafilterConfiguration] : none],
    ['Temperatura del cuarto', item.environment?.temperatureC != null ? `${formatNumber(item.environment.temperatureC, 1)} °C` : none],
    ['Humedad del cuarto', item.environment?.humidityPercent != null ? `${formatNumber(item.environment.humidityPercent, 0)} %` : none],
    ['Notas', item.notes || 'Sin notas'],
  ]
  if (editing) return <PreparationEditor item={item} onDone={() => setEditing(false)}/>
  // «Completar» al lado de cada dato que falta (24-oct-2026), en vez de una
  // franja arriba con la lista: el barista ve qué falta donde va.
  return <div className={styles.stack}>
    <dl className={styles.definition}>{rows.map(([label, value]) => <div className={styles.definitionRow} key={label}><dt>{label}</dt><dd>{value === none ? <span className={styles.missingValue}>{none}<Button variant="quiet" className={styles.completeLink} onPress={() => setEditing(true)}>Completar</Button></span> : value}</dd></div>)}</dl>
    <div className={styles.recipeActions}><Button variant="secondary" onPress={() => setEditing(true)}>Corregir</Button></div>
  </div>
}

// Un solo formulario para los datos y las medidas; al guardar van por sus
// dos rutas del servidor, una tras otra, y solo lo que cambió.
function PreparationEditor({ item, onDone }: { item: Extraction; onDone: () => void }) {
  const queryClient = useQueryClient()
  const toast = useToast()
  const initialContext = contextFromExtraction(item)
  const [context, setContext] = useState(initialContext)
  const measurements = useMeasurementFields(item.manualMeasurements)
  const contextChanged = JSON.stringify(context) !== JSON.stringify(initialContext)
  const save = useMutation({
    mutationFn: async () => {
      if (contextChanged) await resilientPut<Extraction>(`/api/v1/extractions/${item.id}/context`, contextPayload(context))
      if (measurements.hasInput) await resilientPut<Extraction>(`/api/v1/extractions/${item.id}/manual-measurements`, measurements.payload)
    },
    onSuccess: async () => {
      await Promise.all([queryClient.invalidateQueries({ queryKey: ['extraction', item.id] }), queryClient.invalidateQueries({ queryKey: ['extraction-result', item.id] }), queryClient.invalidateQueries({ queryKey: ['extraction-history-record', item.id] })])
      toast.push('Datos guardados', 'El cambio quedó en el historial de la preparación.', 'success')
      onDone()
    },
  })
  return <form className={styles.stack} onSubmit={(event) => { event.preventDefault(); save.mutate() }}>
    {/* Las medidas van entre el café y el cuarto, en las mismas casillas
        iguales que Preparar café (24-oct-2026). */}
    <ContextFields value={context} onChange={setContext} middle={measurements.fields}/>
    {save.error ? <Alert variant="danger" title="No se pudo guardar">{save.error.message}</Alert> : null}
    <div className={styles.recipeActions}><Button variant="quiet" className={styles.recipeUndo} onPress={onDone}>Cancelar</Button><Button type="submit" isDisabled={save.isPending || (!contextChanged && !measurements.hasInput)}>{save.isPending ? 'Guardando…' : 'Guardar cambios'}</Button></div>
  </form>
}

// La banda de cada curva es el objetivo con que se juzgó la preparación, el
// mismo de «Frente al perfil». En una preparación medida la curva es la
// caldera, y su objetivo es la referencia derivada del agua más la subida del
// primer uso; antes se tomaba el agua del perfil (y 93 °C si no venía), y una
// caldera a 105,7 °C se dibujaba contra «93 ± 1 °C».
function objectiveOf(record: ExtractionHistoryRecord, properties: string[], fallback: { target: number; tolerance: number }) {
  const objective = record.result?.properties?.find((item) => properties.includes(item.property))?.objective
  if (!objective) return fallback
  if (objective.target != null) return { target: objective.target, tolerance: objective.tolerance ?? fallback.tolerance }
  if (objective.minimum != null && objective.maximum != null) return { target: (objective.minimum + objective.maximum) / 2, tolerance: (objective.maximum - objective.minimum) / 2 }
  return fallback
}

export function PreparationCurves({ record, loading, error }: { record: ExtractionHistoryRecord | undefined; loading: boolean; error: Error | null }) {
  if (loading) return <LoadingState label="Cargando curvas"/>
  if (error || !record) return <Alert variant="danger" title="No se pudieron cargar las curvas">{error?.message ?? 'Sin datos'}</Alert>
  const boiler = record.extraction.source === 'measured'
  const temperature = objectiveOf(record, boiler ? ['boiler-temperature'] : ['water-temperature'], { target: boiler ? 102 : 93, tolerance: 1 })
  const pressure = objectiveOf(record, ['extraction-pressure', 'pressure'], { target: 9, tolerance: 0.5 })
  const durationMs = record.extraction.durationMs
  return <Surface><h2 className={styles.surfaceTitle}>Curvas</h2>{record.series.samples.length ? <div className={styles.liveCharts}><LiveChart samples={record.series.samples} field="temperatureC" title={boiler ? 'Temperatura de la caldera' : 'Temperatura del agua'} unit="°C" target={temperature.target} tolerance={temperature.tolerance} minimum={temperature.target - temperature.tolerance * 3} maximum={temperature.target + temperature.tolerance * 3} durationMs={durationMs}/><LiveChart samples={record.series.samples} field="pressureBar" title="Presión" unit="bar" target={pressure.target} tolerance={pressure.tolerance} minimum={0} maximum={Math.max(12, pressure.target + pressure.tolerance * 3)} durationMs={durationMs}/></div> : <p>Las lecturas de esta preparación no están disponibles.</p>}</Surface>
}

const sensoryAcceptanceLabels = {
  accepted: 'Aceptada',
  rejected: 'Rechazada',
  inconclusive: 'Inconclusa',
  'not-recorded': 'Sin evaluar',
}


function valueRange(value: { value: number; minimum: number; maximum: number }, unit: string) {
  if (value.minimum === value.maximum) return `${formatNumber(value.value, 2)} ${unit}`.trim()
  return `${formatNumber(value.minimum, 2)}–${formatNumber(value.maximum, 2)} ${unit}`.trim()
}

function objectiveLabel(objective: ExtractionResult['properties'][number]['objective'], unit: string) {
  if (objective.target !== undefined) return `${formatNumber(objective.target, 2)} ${unit}${objective.tolerance ? ` ± ${formatNumber(objective.tolerance, 2)}` : objective.tolerance === null ? ' · sin tolerancia definida' : ''}`.trim()
  return `${formatNumber(objective.minimum, 2)}–${formatNumber(objective.maximum, 2)} ${unit}`.trim()
}

const deviationReasons: Record<string, string> = {
  'pressure-never-reached-range': 'la presión nunca llegó a su rango',
  'no-pressure-range': 'el perfil no da rango de presión para saber desde cuándo juzgar',
}

// La caldera al pulsar se lee en agua: el barista pide el agua y la caldera
// no le dice nada. Mismo veredicto, otras unidades.
function asShown(property: ResultProperty): ResultProperty & { boundText?: string } {
  const water = property.water
  if (!water) return property
  const observed = water.estimatedC != null ? { value: water.estimatedC, minimum: water.estimatedC, maximum: water.estimatedC } : null
  const objective = water.objective.target != null
    ? { target: water.objective.target, tolerance: water.objective.tolerance, minimum: water.objective.minimum, maximum: water.objective.maximum }
    : { minimum: water.objective.minimum, maximum: water.objective.maximum }
  return { ...property, unit: '°C', objective, observed, deviation: { ...property.deviation, amount: water.deviationAmountC }, ...(water.bound ? { boundText: boundText(water.bound) } : {}) }
}

function deviationLabel(property: ExtractionResult['properties'][number]) {
  const { deviation } = property
  if (deviation.status === 'unavailable') return 'Sin datos'
  if (deviation.status === 'no-tolerance') return 'Sin tolerancia definida'
  if (deviation.status === 'not-evaluable') return `Sin evaluar: ${deviationReasons[deviation.reason ?? ''] ?? 'falta el tramo en que juzgarla'}`
  if (deviation.status === 'within') return 'Dentro del objetivo'
  if (deviation.status !== 'outside') return 'Sin evaluar'
  const direction = deviation.direction === 'below' ? 'por debajo' : deviation.direction === 'above' ? 'por encima' : 'fuera por ambos límites'
  const amount = `${formatNumber(deviation.amount, 2)} ${property.unit} ${direction}`.trim()
  const when = deviation.firstOutsideMs != null ? `; desde los ${formatNumber(deviation.firstOutsideMs / 1000, 1)} s, ${formatNumber((deviation.outsideDurationMs ?? 0) / 1000, 1)} s fuera` : ''
  const reason = deviation.reason ? ` (${deviationReasons[deviation.reason] ?? deviation.reason})` : ''
  return `${amount}${when}${reason}`
}

// Objetivo, observado y diferencia por propiedad. El diagnóstico largo y la
// estabilidad de la máquina están en «Detalles técnicos».
function ExtractionResultPanel({ result }: { result: ExtractionResult }) {
  return <div className={styles.resultPanel}>
    {result.technicalConformance.missingData.length ? <p className={styles.formHint}>Sin juzgar: {result.technicalConformance.missingData.join(', ').toLocaleLowerCase()}.</p> : null}
    <div className={styles.resultProperties} role="table" aria-label="Objetivos, valores observados y diferencias">
      <div className={styles.resultPropertyHeader} role="row"><span role="columnheader">Valor</span><span role="columnheader">Objetivo</span><span role="columnheader">Salió</span><span role="columnheader">Diferencia</span></div>
      {result.properties.map(asShown).map((property) => <div className={styles.resultProperty} role="row" key={property.property}><strong role="cell">{property.label}</strong><span role="cell">{objectiveLabel(property.objective, property.unit)}</span><span role="cell">{property.observed ? valueRange(property.observed, property.unit) : property.boundText ?? 'Sin información'}</span><span role="cell" data-state={property.deviation.status}>{deviationLabel(property)}</span></div>)}
    </div>
  </div>
}

const emptyReadings = ['', '', '']

function numberText(value: number | undefined | null) {
  return value == null ? '' : String(value)
}

/* Los campos de las medidas a mano, para el formulario de la preparación.
   Parte de lo guardado y manda solo lo que cambió, para que la revisión diga
   qué se corrigió. Un campo vaciado no
   borra el valor guardado: el servidor no admite quitar una medida, solo
   reemplazarla. */
function useMeasurementFields(initial: ManualMeasurements) {
  const [dose, setDose] = useState(numberText(initial?.coffeeDoseG))
  const [mass, setMass] = useState(numberText(initial?.beverageMassG))
  const [readings, setReadings] = useState<string[]>(() => emptyReadings.map((empty, index) => numberText(initial?.tdsReadings?.[index]) || empty))
  const filledReadings = readings.filter((reading) => reading !== '').map(Number)
  // La misma media que calcula el servidor; aquí solo se muestra.
  const readingsMean = filledReadings.length ? filledReadings.reduce((sum, reading) => sum + reading, 0) / filledReadings.length : null
  const payload: ManualMeasurementsPayload = {}
  if (dose && Number(dose) !== initial?.coffeeDoseG) payload.coffeeDoseG = Number(dose)
  if (mass && Number(mass) !== initial?.beverageMassG) payload.beverageMassG = Number(mass)
  if (filledReadings.length && JSON.stringify(filledReadings) !== JSON.stringify(initial?.tdsReadings ?? [])) payload.tdsReadings = filledReadings
  const hasInput = Object.keys(payload).length > 0
  /* Lo que se mide fuera de la máquina, en casillas iguales (24-oct-2026).
     La media del TDS ya no se escribe: la calcula el servidor con las
     lecturas, y una sola lectura basta. Sin lecturas, un café viejo que solo
     trae la media la muestra. El instrumento no se pide: es siempre el mismo
     refractómetro y lo que ya traía se conserva. */
  const meanText = readingsMean !== null ? `TDS medio: ${formatNumber(readingsMean, 2)} % (de ${filledReadings.length} lectura${filledReadings.length === 1 ? '' : 's'})`
    : initial?.tdsPercent != null ? `TDS medio guardado: ${formatNumber(initial.tdsPercent, 2)} %, sin lecturas` : ''
  const fields = <>
    <div className={styles.fieldUniform}><Field label="Dosis (g)"><NumberField min="0.1" max="100" step="0.1" value={dose} onChange={(event) => setDose(event.target.value)}/></Field></div>
    <div className={styles.fieldUniform}><Field label="Masa en taza (g)"><NumberField min="0.1" max="300" step="0.1" value={mass} onChange={(event) => setMass(event.target.value)}/></Field></div>
    {readings.map((reading, index) => <div key={index} className={styles.fieldUniform}><Field label={`TDS, lectura ${index + 1} (%)`}><NumberField min="0.01" max="30" step="0.01" value={reading} onChange={(event) => setReadings((current) => current.map((item, position) => position === index ? event.target.value : item))}/></Field></div>)}
    {meanText ? <p className={styles.uniformNote}>{meanText}</p> : null}
  </>
  return { fields, payload, hasInput }
}

function SensoryReferences({ coffee, profile }: { coffee: Coffee | undefined; profile: PreparationProfile | undefined }) {
  const declared = coffee?.declaredSensoryProfile
  const target = profile?.sensoryTarget
  return <div className={styles.sensoryReferenceGrid}>
    <section><strong>Descripción del productor o tostador</strong>{declared ? <><p>{declared.originalText}</p><small>{declared.declaredBy} · {declared.source}</small></> : <p>No hay una descripción declarada para este café.</p>}</section>
    <section><strong>Objetivo del barista</strong>{target ? <><p>{target.rationale}</p><small>{target.descriptors.map(sensoryDescriptorLabel).join(' · ')}</small></> : <p>No hay un objetivo definido para este perfil.</p>}</section>
  </div>
}

// La cata en una mirada: la decisión, los defectos y los descriptores como
// etiquetas, y las puntuaciones en una sola tabla corta (23-oct-2026).
function SensoryEvaluationSummary({ evaluation }: { evaluation: NonNullable<Extraction['sensoryEvaluation']> }) {
  const acceptanceLabels = { accepted: 'Aceptada', rejected: 'Rechazada', inconclusive: 'Inconclusa' }
  const attributes = [...new Set(evaluation.scores.map((score) => score.attribute))]
  const scoreOf = (attribute: string, dimension: string) => evaluation.scores.find((score) => score.attribute === attribute && score.dimension === dimension)?.value
  return <div className={styles.stack}>
    <div><strong>{acceptanceLabels[evaluation.acceptance.status]}</strong>{evaluation.acceptance.rationale ? <span>: {evaluation.acceptance.rationale}</span> : null}</div>
    {evaluation.defects.length ? <div className={styles.chipGroup}><span>Defectos</span><div>{evaluation.defects.map((defect) => <span key={defect} className={`${styles.chipStatic} ${styles.chipWarn}`}>{sensoryDefectLabel(defect)}</span>)}</div></div> : null}
    {evaluation.descriptorRatings?.length || evaluation.descriptors?.length || evaluation.otherDescriptors?.length ? <div className={styles.chipGroup}><span>Descriptores</span><div>{(evaluation.descriptorRatings?.length ? evaluation.descriptorRatings : (evaluation.descriptors ?? []).map((descriptor) => ({ descriptor, intensity: null }))).map((rating) => <span key={rating.descriptor} className={styles.chipStatic}>{sensoryDescriptorLabel(rating.descriptor)}{rating.intensity != null ? ` · ${rating.intensity}/15` : ''}</span>)}{(evaluation.otherDescriptors ?? []).map((item) => <span key={`otro-${item.name}`} className={styles.chipStatic}>{item.name} · {item.intensity}/15</span>)}</div></div> : null}
    {evaluation.mainTastes?.length ? <div className={styles.chipGroup}><span>Sabores básicos</span><div>{evaluation.mainTastes.map((taste) => <span key={taste} className={styles.chipStatic}>{basicTastes.find((item) => item.value === taste)?.label ?? taste}</span>)}</div></div> : null}
    {evaluation.mouthfeelTextures?.length ? <div className={styles.chipGroup}><span>En boca</span><div>{evaluation.mouthfeelTextures.map((texture) => <span key={texture} className={styles.chipStatic}>{mouthfeelTextures.find((item) => item.value === texture)?.label ?? texture}</span>)}</div></div> : null}
    {evaluation.desiredChanges?.length ? <div className={styles.chipGroup}><span>Quiere</span><div>{evaluation.desiredChanges.map((change) => <span key={change.attribute} className={styles.chipStatic}>{sensoryDirectionLabel(change.direction)} {sensoryAttributeLabel(change.attribute).toLocaleLowerCase()}</span>)}</div></div> : null}
    {attributes.length ? <div className={styles.scoreTable} role="table" aria-label="Puntuaciones de la cata"><div role="row"><span role="columnheader">Atributo</span><span role="columnheader">Intensidad (0–15)</span><span role="columnheader">Agrado (1–9)</span></div>{attributes.map((attribute) => <div role="row" key={attribute}><span role="cell">{sensoryAttributeLabel(attribute)}</span><span role="cell">{scoreOf(attribute, 'perceived-intensity') ?? '—'}</span><span role="cell">{scoreOf(attribute, 'hedonic-liking') ?? '—'}</span></div>)}</div> : null}
    {evaluation.comment ? <p>{evaluation.comment}</p> : null}
    <p className={styles.meta}>{evaluation.evaluatedBy} · {formatDateTime(evaluation.evaluatedAt)}{evaluation.minutesSinceExtraction != null ? ` · ${formatNumber(evaluation.minutesSinceExtraction, 1)} min después` : ''}{evaluation.servingTemperatureC != null ? ` · servida a ${formatNumber(evaluation.servingTemperatureC, 1)} °C` : ''}</p>
  </div>
}

/* La ficha de cata en el orden en que el barista cata (23-oct-2026): el
   agrado global; los descriptores y los defectos presentados como en la ficha
   CVA de la SCA (familias con casillas, hasta cinco descriptores, defectos en
   un recuadro aparte con «Ninguno»); las escalas como círculos que se tocan;
   los cambios deseados y, al final, la decisión, porque aceptar o rechazar se
   decide después de evaluar. Antes eran listas desplegables, «Agregar
   descriptor» y tres cajas con el nombre repetido por cada atributo.

   Los cambios deseados son solo los de atributos que pueden ser defecto, igual
   que en el sabor buscado del perfil; un cambio guardado sobre otro atributo,
   o «Mejorar», se conserva sin mostrarse. Nada lleva valor de partida: un 7 o
   un 5 que el barista no tocó quedarían como calificación suya. */
interface SensoryDraft {
  acceptance: string
  rationale: string
  liking: string
  defects: string[]
  descriptors: Record<string, string>
  // Los que no están en la lista, escritos por el barista, con su intensidad.
  others?: Array<{ name: string; intensity: string }>
  mainTastes?: string[]
  mouthfeelTextures?: string[]
  scores: Record<string, string>
  changes: Record<string, string>
  comment: string
  servingTemperature: string
  minutesSince: string
}

const CHANGE_ATTRIBUTES = ['Acidity', 'Bitterness', 'Astringency']
const CHANGE_CHOICES = [{ value: 'Decrease', label: 'Menos' }, { value: '', label: 'Igual' }, { value: 'Increase', label: 'Más' }]

function draftFromEvaluation(evaluation: Extraction['sensoryEvaluation'] | null | undefined): SensoryDraft {
  const scores: Record<string, string> = {}
  let liking = ''
  for (const score of evaluation?.scores ?? []) {
    if (score.attribute === 'OverallLiking') liking = String(score.value)
    else scores[`${score.attribute}-${score.dimension === 'hedonic-liking' ? 'liking' : 'intensity'}`] = String(score.value)
  }
  const descriptors: Record<string, string> = {}
  for (const descriptor of evaluation?.descriptors ?? []) descriptors[descriptor] = ''
  for (const rating of evaluation?.descriptorRatings ?? []) descriptors[rating.descriptor] = String(rating.intensity)
  return {
    acceptance: evaluation?.acceptance.status ?? '',
    rationale: evaluation?.acceptance.rationale ?? '',
    liking,
    defects: [...(evaluation?.defects ?? [])],
    descriptors,
    others: (evaluation?.otherDescriptors ?? []).map((item) => ({ name: item.name, intensity: String(item.intensity) })),
    mainTastes: [...(evaluation?.mainTastes ?? [])],
    mouthfeelTextures: [...(evaluation?.mouthfeelTextures ?? [])],
    scores,
    changes: Object.fromEntries((evaluation?.desiredChanges ?? []).map((change) => [change.attribute, change.direction])),
    comment: evaluation?.comment ?? '',
    servingTemperature: evaluation?.servingTemperatureC != null ? String(evaluation.servingTemperatureC) : '',
    minutesSince: evaluation?.minutesSinceExtraction != null ? String(evaluation.minutesSinceExtraction) : '',
  }
}

function readSensoryDraft(key: string): SensoryDraft | null {
  try {
    const stored = localStorage.getItem(key)
    return stored ? JSON.parse(stored) as SensoryDraft : null
  } catch { return null }
}

const MAX_DESCRIPTORS = 5

// Una escala como en la ficha CVA: círculos que se tocan, uno por valor. Tocar
// el marcado lo quita, para que «sin calificar» siga siendo posible: un
// número puesto por error no se puede borrar de otra forma.
function CircleScale({ label, min, max, value, onChange }: { label: string; min: number; max: number; value: string; onChange: (value: string) => void }) {
  return <div role="group" aria-label={label} className={styles.circleScale}>{Array.from({ length: max - min + 1 }, (_, index) => String(min + index)).map((option) => { const on = value === option; return <Button key={option} variant="quiet" className={on ? `${styles.circle} ${styles.circleOn}` : styles.circle} aria-pressed={on} aria-label={`${label}: ${option}`} onPress={() => onChange(on ? '' : option)}>{option}</Button> })}</div>
}

function ChoiceRow({ label, options, value, onChange }: { label: string; options: Array<{ value: string; label: string }>; value: string; onChange: (value: string) => void }) {
  return <div role="group" aria-label={label} className={styles.choiceRow}>{options.map((option) => { const on = value === option.value; return <Button key={option.label} variant="quiet" className={on ? `${styles.chip} ${styles.chipOn}` : styles.chip} aria-pressed={on} onPress={() => onChange(option.value)}>{option.label}</Button> })}</div>
}

function SensoryForm({ draftKey, initial, hasDeclaredReference, hasTargetReference, pending, error, onSubmit, onCancel }: { draftKey: string; initial?: Extraction['sensoryEvaluation'] | null; hasDeclaredReference: boolean; hasTargetReference: boolean; pending: boolean; error: Error | null; onSubmit: (payload: Record<string, unknown>) => void; onCancel?: (() => void) | undefined }) {
  // El borrador es de este formato; uno del formato anterior no se lee.
  const key = `${draftKey}:v2`
  const [recovered] = useState(() => readSensoryDraft(key))
  const [draft, setDraft] = useState<SensoryDraft>(() => recovered ?? draftFromEvaluation(initial))
  const [formError, setFormError] = useState('')
  // La hora de la evaluación es la de ahora: la ficha se llena al catar. No va
  // en el borrador, porque al recuperarlo días después sería la de entonces.
  const [evaluatedAt] = useState(() => new Date().toISOString())
  useEffect(() => {
    try { localStorage.setItem(key, JSON.stringify(draft)) } catch { /* El formulario sigue disponible si el navegador rechaza el almacenamiento. */ }
  }, [draft, key])
  const set = (change: Partial<SensoryDraft>) => setDraft((current) => ({ ...current, ...change }))
  const toggle = (list: 'defects', value: string) => set({ [list]: draft[list].includes(value) ? draft[list].filter((item) => item !== value) : [...draft[list], value] })
  const others = draft.others ?? []
  const [otherName, setOtherName] = useState('')
  const chosenCount = Object.keys(draft.descriptors).length + others.length
  const addOther = () => {
    const name = otherName.trim()
    if (!name || chosenCount >= MAX_DESCRIPTORS || others.some((item) => item.name.toLocaleLowerCase() === name.toLocaleLowerCase())) return
    set({ others: [...others, { name, intensity: '' }] })
    setOtherName('')
  }
  const toggleDescriptor = (value: string) => { const next = { ...draft.descriptors }; if (value in next) delete next[value]; else next[value] = ''; set({ descriptors: next }) }
  const submit = () => {
    if (!draft.acceptance) return setFormError('Elige la decisión sobre la bebida.')
    if (draft.acceptance === 'rejected' && !draft.rationale.trim()) return setFormError('Escribe por qué se rechazó.')
    if (!draft.liking) return setFormError('Elige el agrado global.')
    const missingIntensity = Object.entries(draft.descriptors).find(([, intensity]) => intensity === '')
    if (missingIntensity) return setFormError(`Indica la intensidad de ${sensoryDescriptorLabel(missingIntensity[0]).toLocaleLowerCase()}.`)
    const missingOther = others.find((item) => item.intensity === '')
    if (missingOther) return setFormError(`Indica la intensidad de ${missingOther.name.toLocaleLowerCase()}.`)
    setFormError('')
    const scores = [{ attribute: 'OverallLiking', dimension: 'hedonic-liking', value: Number(draft.liking) }]
    sensoryAttributes.forEach((attribute) => {
      const intensity = draft.scores[`${attribute.value}-intensity`]
      const liking = draft.scores[`${attribute.value}-liking`]
      if (intensity && attribute.value !== 'Balance') scores.push({ attribute: attribute.value, dimension: 'perceived-intensity', value: Number(intensity) })
      if (liking) scores.push({ attribute: attribute.value, dimension: 'hedonic-liking', value: Number(liking) })
    })
    const names = Object.keys(draft.descriptors)
    onSubmit({
      id: `eval-${Date.now()}`,
      evaluatedAt,
      protocolReference: sensoryProtocolReference,
      servingTemperatureC: draft.servingTemperature === '' ? null : Number(draft.servingTemperature),
      minutesSinceExtraction: draft.minutesSince === '' ? null : Number(draft.minutesSince),
      descriptors: names,
      descriptorRatings: names.map((name) => ({ descriptor: name, intensity: Number(draft.descriptors[name]) })),
      otherDescriptors: others.map((item) => ({ name: item.name, intensity: Number(item.intensity) })),
      mainTastes: draft.mainTastes ?? [],
      mouthfeelTextures: draft.mouthfeelTextures ?? [],
      defects: draft.defects,
      desiredChanges: Object.entries(draft.changes).filter(([, direction]) => direction).map(([attribute, direction]) => ({ attribute, direction })),
      comparisons: [...(hasDeclaredReference ? ['declared-profile'] : []), ...(hasTargetReference ? ['barista-target'] : [])],
      scores,
      acceptance: { status: draft.acceptance, rationale: draft.rationale.trim() || null },
      comment: draft.comment,
    })
  }
  return <form className={styles.cataForm} onSubmit={(event) => { event.preventDefault(); submit() }}>
    {recovered ? <p className={styles.formHint}>Recuperamos la cata que habías dejado sin terminar en este navegador.</p> : null}
    <fieldset className={styles.defectBox}><legend>Defectos (si hay)</legend><div><Checkbox label="Ninguno" checked={!draft.defects.length} onChange={() => set({ defects: [] })}/>{sensoryDefects.map((defect) => <Checkbox key={defect.value} label={defect.label} checked={draft.defects.includes(defect.value)} onChange={() => toggle('defects', defect.value)}/>)}</div></fieldset>
    <section><h3 className={styles.subTitle}>Descriptores <span className={styles.meta}>{chosenCount} de {MAX_DESCRIPTORS}</span></h3>
      <div className={styles.descriptorFamilies}>{sensoryDescriptorGroups.map((group) => <div key={group.label}><strong>{group.label}</strong><div>{group.options.map((option) => { const on = option.value in draft.descriptors; return <Checkbox key={option.value} label={option.label} checked={on} disabled={!on && chosenCount >= MAX_DESCRIPTORS} onChange={() => toggleDescriptor(option.value)}/> })}</div></div>)}</div>
      <div className={styles.otherDescriptor}><Field label="Otro descriptor" description="si no está en la lista, como lo dijo el barista"><Input maxLength={60} value={otherName} placeholder="ej. azúcar morena" onChange={(event) => setOtherName(event.target.value)} onKeyDown={(event) => { if (event.key === 'Enter') { event.preventDefault(); addOther() } }}/></Field><Button variant="secondary" isDisabled={!otherName.trim() || chosenCount >= MAX_DESCRIPTORS} onPress={addOther}>Agregar</Button></div>
      {chosenCount ? <div className={styles.scaleTable}>{Object.keys(draft.descriptors).map((name) => <div key={name}><span>{sensoryDescriptorLabel(name)} <small>intensidad</small></span><CircleScale label={`Intensidad de ${sensoryDescriptorLabel(name)}`} min={0} max={15} value={draft.descriptors[name] ?? ''} onChange={(intensity) => set({ descriptors: { ...draft.descriptors, [name]: intensity } })}/></div>)}{others.map((item, index) => <div key={item.name}><span>{item.name} <small>intensidad · <Button variant="quiet" className={styles.inlineLink} onPress={() => set({ others: others.filter((_, position) => position !== index) })}>quitar</Button></small></span><CircleScale label={`Intensidad de ${item.name}`} min={0} max={15} value={item.intensity} onChange={(intensity) => set({ others: others.map((other, position) => position === index ? { ...other, intensity } : other) })}/></div>)}</div> : null}
    </section>
    <section><h3 className={styles.subTitle}>Sabores básicos <span className={styles.meta}>hasta dos</span></h3><div className={styles.choiceRow}>{basicTastes.map((taste) => { const on = (draft.mainTastes ?? []).includes(taste.value); return <Checkbox key={taste.value} label={taste.label} checked={on} disabled={!on && (draft.mainTastes ?? []).length >= 2} onChange={() => set({ mainTastes: on ? (draft.mainTastes ?? []).filter((item) => item !== taste.value) : [...(draft.mainTastes ?? []), taste.value] })}/> })}</div></section>
    <section><h3 className={styles.subTitle}>Sensación en boca <span className={styles.meta}>hasta dos</span></h3><div className={styles.choiceRow}>{mouthfeelTextures.map((texture) => { const on = (draft.mouthfeelTextures ?? []).includes(texture.value); return <Checkbox key={texture.value} label={texture.label} checked={on} disabled={!on && (draft.mouthfeelTextures ?? []).length >= 2} onChange={() => set({ mouthfeelTextures: on ? (draft.mouthfeelTextures ?? []).filter((item) => item !== texture.value) : [...(draft.mouthfeelTextures ?? []), texture.value] })}/> })}</div></section>
    <section><h3 className={styles.subTitle}>Ficha <span className={styles.meta}>intensidad 0 ausente · 15 máxima; agrado 1 me desagrada mucho · 9 me agrada mucho</span></h3><div className={styles.scaleTable}>{sensoryAttributes.map((attribute) => <div key={attribute.value}><span>{attribute.label}</span><div className={styles.scalePair}>{attribute.value === 'Balance' ? null : <div><small>Intensidad</small><CircleScale label={`Intensidad de ${attribute.label}`} min={0} max={15} value={draft.scores[`${attribute.value}-intensity`] ?? ''} onChange={(value) => set({ scores: { ...draft.scores, [`${attribute.value}-intensity`]: value } })}/></div>}<div><small>Agrado</small><CircleScale label={`Agrado de ${attribute.label}`} min={1} max={9} value={draft.scores[`${attribute.value}-liking`] ?? ''} onChange={(value) => set({ scores: { ...draft.scores, [`${attribute.value}-liking`]: value } })}/></div></div></div>)}<div className={styles.globalRow}><span>Global</span><CircleScale label="Agrado global" min={1} max={9} value={draft.liking} onChange={(liking) => set({ liking })}/></div></div></section>
    <section><h3 className={styles.subTitle}>Cambios deseados</h3><div className={styles.directionTable}>{sensoryAttributes.filter((attribute) => CHANGE_ATTRIBUTES.includes(attribute.value)).map((attribute) => <div key={attribute.value} className={styles.directionRow}><span>{attribute.label}</span><ChoiceRow label={attribute.label} value={draft.changes[attribute.value] ?? ''} options={CHANGE_CHOICES} onChange={(direction) => { const changes = { ...draft.changes }; if (direction) changes[attribute.value] = direction; else delete changes[attribute.value]; set({ changes }) }}/></div>)}</div></section>
    <Field label="Comentario"><TextArea value={draft.comment} onChange={(event) => set({ comment: event.target.value })}/></Field>
    <section><h3 className={styles.subTitle}>Decisión</h3><ChoiceRow label="Decisión" value={draft.acceptance} onChange={(acceptance) => set({ acceptance })} options={[{ value: 'accepted', label: 'Aceptado' }, { value: 'rejected', label: 'Rechazado' }, { value: 'inconclusive', label: 'No concluyente' }]}/>
      {draft.acceptance === 'rejected' ? <Field label="¿Por qué?"><TextArea value={draft.rationale} onChange={(event) => set({ rationale: event.target.value })}/></Field> : null}</section>
    <details className={styles.profileAdvanced}><summary>Más datos</summary><div className={styles.formGrid}>
      <Field label="Minutos desde la extracción"><NumberField min="0" step="0.5" value={draft.minutesSince} onChange={(event) => set({ minutesSince: event.target.value })}/></Field>
      <Field label="Temperatura de servicio (°C)"><NumberField min="0" max="100" step="0.1" value={draft.servingTemperature} onChange={(event) => set({ servingTemperature: event.target.value })}/></Field>
    </div></details>
    {formError ? <p className={styles.formError} role="alert">{formError}</p> : null}
    {error ? <Alert variant="danger" title="No se pudo guardar">{error.message}</Alert> : null}
    <div className={styles.recipeActions}>{onCancel ? <Button variant="quiet" className={styles.recipeUndo} onPress={onCancel}>Cancelar</Button> : null}<Button type="submit" isDisabled={pending}>{pending ? 'Guardando…' : 'Guardar cata'}</Button></div>
  </form>
}
