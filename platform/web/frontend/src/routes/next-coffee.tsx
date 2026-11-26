import { useEffect, useState } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { api, post } from '@/shared/api/client'
import type { Episode, Extraction, ExtractionHistoryRecord, Recommendation } from '@/shared/api/types'
import { Alert, Button, Field, Input, NumberField, TextArea } from '@/shared/ui'
import { formatNumber } from '@/shared/lib/format'
import { interruptedByPowerLoss } from './preparation-status'
import { sensoryDefectLabel } from './sensory-options'
import styles from './routes.module.css'

/* «Para el próximo café» (23-oct-2026): la recomendación, la decisión del
   barista, el ajuste y su comprobación, por pasos y mostrando solo el paso en
   que va. Vive dentro de la cata y aparece al guardarla: es lo que el barista
   hace justo después de evaluar la taza. Antes eran tres páginas
   (Recomendaciones, Ajustes y el detalle) y la abstención del razonador se
   leía como si fuera una recomendación, con su enlace «Ver recomendación».

   La comprobación es el café siguiente que el barista prepara en la máquina:
   Preparar café lo enlaza al llegar. Ya no se abre una preparación del
   simulador para comprobar, porque no decía nada de la máquina. */

const PROPERTIES = [
  { value: 'water-temperature', label: 'Temperatura del agua', unit: '°C' },
  { value: 'extraction-time', label: 'Tiempo máximo', unit: 's' },
  { value: 'coffee-dose', label: 'Dosis', unit: 'g' },
  { value: 'grind-setting', label: 'Molienda', unit: 'clics' },
  { value: 'pressure', label: 'Presión', unit: 'bar' },
  { value: 'brew-ratio', label: 'Ratio', unit: ':1' },
] as const

const OUTCOMES = [
  { value: 'expected-observed', label: 'Sí' },
  { value: 'insufficient-observed', label: 'No alcanzó' },
  { value: 'adverse-observed', label: 'Empeoró' },
  { value: 'inconclusive', label: 'No se sabe' },
]

// Por qué no hay recomendación, en palabras del barista. Lo demás lo dice el
// razonador con su propio texto.
const ABSTENTIONS: Record<string, string> = {
  PREPARATION_NOT_MEASURED: 'Este café no se puede analizar: solo se analizan cafés completos hechos en la máquina.',
  NO_REASONER_CONCLUSION: 'No se encontró nada que ajustar en este café.',
  NO_MEASURABLE_CAUSE: 'Con lo que mide la máquina no se encuentra una causa. Decide tú qué probar en el siguiente café.',
}

function propertyLabel(property: string) {
  return PROPERTIES.find((item) => item.value === property)?.label ?? property
}

// El valor con que se hizo este café, para que el ajuste parta de él: la
// molienda y la dosis las anotó el barista; tiempo, agua y ratio, el perfil.
function previousValue(item: Extraction, property: string): string {
  if (property === 'grind-setting') return item.grindSetting != null ? String(item.grindSetting) : ''
  if (property === 'coffee-dose') return item.manualMeasurements.coffeeDoseG != null ? String(item.manualMeasurements.coffeeDoseG) : ''
  const specifications = (item.profile.specifications ?? []) as Array<{ property: string; strategy: string; target?: number | null; maximum?: number | null }>
  const names = property === 'extraction-time' ? ['extraction-time'] : property === 'water-temperature' ? ['water-temperature'] : property === 'pressure' ? ['extraction-pressure', 'pressure'] : ['brew-ratio']
  const spec = specifications.find((candidate) => names.includes(candidate.property))
  const value = property === 'extraction-time' ? spec?.maximum : spec?.target
  return value != null ? String(value) : ''
}

export function NextCoffee({ item, record }: { item: Extraction; record: ExtractionHistoryRecord | undefined }) {
  const episodes = record?.episodes ?? []
  const check = episodes.find((episode) => episode.followUpExtractionId === item.id)
  const own = episodes.find((episode) => episode.initialExtractionId === item.id)
  // Un café de comprobación primero responde si el ajuste funcionó; después,
  // como cualquier otro, puede llevar su propio ajuste: la cadena sigue.
  const checkStep = check ? <CheckOutcome episode={check}/> : null
  if (check && !check.outcome) return checkStep
  const next = own ? <RegisteredAdjustment episode={own}/>
    : item.source !== 'measured' || interruptedByPowerLoss(item) ? <p className={styles.muted}>{ABSTENTIONS.PREPARATION_NOT_MEASURED}</p>
      : <RecommendationStep item={item} record={record}/>
  return <div className={styles.stack}>{checkStep}{next}</div>
}

function RecommendationStep({ item, record }: { item: Extraction; record: ExtractionHistoryRecord | undefined }) {
  const queryClient = useQueryClient()
  const latestId = [...(record?.recommendations ?? [])].sort((a, b) => a.createdAt.localeCompare(b.createdAt)).at(-1)?.recommendationId
  const recommendation = useQuery({ queryKey: ['recommendation', latestId], queryFn: () => api<Recommendation>(`/api/v1/recommendations/${latestId}`), enabled: Boolean(latestId) })
  const refresh = () => Promise.all([queryClient.invalidateQueries({ queryKey: ['extraction-history-record', item.id] }), queryClient.invalidateQueries({ queryKey: ['extraction-result', item.id] }), queryClient.invalidateQueries({ queryKey: ['recommendation'] })])
  const ask = useMutation({ mutationFn: () => post<Recommendation>('/api/v1/recommendations', { extractionId: item.id }), onSuccess: refresh })
  const [changing, setChanging] = useState(false)
  const [registering, setRegistering] = useState(false)
  const decide = useMutation({ mutationFn: (payload: Record<string, unknown>) => post<Recommendation>(`/api/v1/recommendations/${latestId}/decision`, payload), onSuccess: refresh })
  const tasted = Boolean(item.sensoryEvaluation)
  const current = recommendation.data
  // El ajuste lo registra el barista con o sin recomendación: la propuesta del
  // razonador es una ayuda, no un requisito para cambiar algo.
  if (registering) return <AdjustmentForm item={item} onCancel={() => setRegistering(false)}/>
  /* Una pregunta en vez de un título (24-oct-2026): «Para el próximo café»
     no decía qué se esperaba del barista. Responde sí y anota el cambio, o
     pide ayuda al razonador; si no cambia nada, no toca ningún botón. */
  const startButtons = (label: string) => <div className={styles.nextCoffeeChoices}>
    <Button isDisabled={!tasted} onPress={() => setRegistering(true)}>Sí, hacer un ajuste</Button>
    <Button variant="secondary" isDisabled={!tasted || ask.isPending} onPress={() => ask.mutate()}>{ask.isPending ? 'Pidiendo…' : label}</Button>
  </div>
  if (!current || current.status === 'abstained') return <div className={styles.stack}>
    <h3 className={styles.subTitle}>¿Necesitas hacer un ajuste para el siguiente café?</h3>
    <p className={styles.muted}>{current ? ABSTENTIONS[current.reasonCode] ?? current.summary : 'Anótalo aquí y el siguiente café te dirá si funcionó. Si todo quedó bien, sigue sin tocar nada.'}</p>
    {startButtons(current ? 'Recomiéndame otra cosa' : '¿Qué me recomiendas?')}
    {ask.error ? <Alert variant="danger" title="No se pudo pedir la recomendación">{ask.error.message}</Alert> : null}
  </div>
  const decision = current.decision
  return <div className={styles.stack}>
    <div className={styles.recommendationBox}>
      <strong>{current.summary}</strong>
      {current.heuristic ? <p className={styles.meta}>{current.heuristic.description} · {current.heuristic.source} · confianza {current.heuristic.confidence}</p> : current.diagnosis?.summary ? <p className={styles.meta}>{current.diagnosis.summary}</p> : null}
    </div>
    {!decision && !changing ? <div className={styles.recipeActions}>
      <Button variant="quiet" className={styles.recipeUndo} isDisabled={decide.isPending} onPress={() => decide.mutate({ status: 'rejected', comment: null, modifiedAdjustment: null })}>No seguirla</Button>
      <Button variant="secondary" isDisabled={decide.isPending} onPress={() => setChanging(true)}>Cambiarla</Button>
      <Button isDisabled={decide.isPending} onPress={() => decide.mutate({ status: 'accepted', comment: null, modifiedAdjustment: null })}>Seguirla</Button>
    </div> : null}
    {decide.error ? <Alert variant="danger" title="No se pudo guardar la decisión">{decide.error.message}</Alert> : null}
    {decision?.status === 'rejected' ? <><p className={styles.muted}>No seguiste esta recomendación.</p><div className={styles.recipeActions}><Button variant="secondary" onPress={() => setRegistering(true)}>Registrar ajuste</Button></div></> : null}
    {decision && decision.status !== 'rejected' ? <AdjustmentForm item={item} recommendation={current}/> : null}
    {changing && !decision ? <AdjustmentForm item={item} recommendation={current} modify={(payload) => decide.mutateAsync(payload)} onCancel={() => setChanging(false)}/> : null}
  </div>
}

function AdjustmentForm({ item, recommendation, modify, onCancel }: { item: Extraction; recommendation?: Recommendation; modify?: (payload: Record<string, unknown>) => Promise<unknown>; onCancel?: () => void }) {
  const queryClient = useQueryClient()
  const proposed = recommendation ? recommendation.decision?.modifiedAdjustment ?? recommendation.proposedAdjustment : null
  const startProperty = PROPERTIES.some((option) => option.value === proposed?.property) ? proposed!.property : 'water-temperature'
  const [property, setProperty] = useState(startProperty)
  const recorded = previousValue(item, property)
  // Si el café no trae el valor (la molienda que no se anotó), el barista lo
  // escribe aquí mismo en vez de quedar bloqueado y tener que ir a otra
  // sección (24-oct-2026).
  const [typedPrevious, setTypedPrevious] = useState('')
  const previous = recorded !== '' ? recorded : typedPrevious
  const [newValue, setNewValue] = useState('')
  const [defect, setDefect] = useState(item.sensoryEvaluation?.defects[0] ?? '')
  const [reason, setReason] = useState('')
  // El valor nuevo lo escribe el barista. El número que trae la propuesta del
  // razonador es el que pide el perfil para esa variable, no cuánto moverla:
  // sumarlo al valor de este café daba un ajuste que nadie propuso.
  useEffect(() => { setNewValue(''); setTypedPrevious('') }, [property])
  const unit = PROPERTIES.find((option) => option.value === property)?.unit ?? ''
  const save = useMutation({
    mutationFn: async () => {
      if (modify) {
        const delta = Number(newValue) - Number(previous)
        await modify({ status: 'modified', comment: reason.trim(), modifiedAdjustment: { property, direction: delta < 0 ? 'decrease' : delta > 0 ? 'increase' : 'maintain', value: Math.abs(delta), unit } })
      }
      return post<Episode>('/api/v1/learning-episodes/adjustments', {
        initialExtractionId: item.id,
        adjustment: { property, previousValue: Number(previous), newValue: Number(newValue), unit, occurredAt: new Date().toISOString(), rationale: reason.trim() || null, recommendationId: recommendation?.recommendationId ?? null },
        targetOutcome: defect ? { kind: 'sensory-defect', code: defect } : { kind: 'observable-property', code: property },
      })
    },
    onSuccess: () => Promise.all([queryClient.invalidateQueries({ queryKey: ['extraction-history-record', item.id] }), queryClient.invalidateQueries({ queryKey: ['episodes'] }), queryClient.invalidateQueries({ queryKey: ['recommendation'] })]),
  })
  const defects = item.sensoryEvaluation?.defects ?? []
  const same = previous !== '' && newValue !== '' && Number(newValue) === Number(previous)
  const ready = previous !== '' && newValue !== '' && !same && (!modify || Boolean(reason.trim()))
  const chip = (on: boolean) => on ? `${styles.chip} ${styles.chipOn}` : styles.chip
  /* Tres preguntas en orden, como se piensa el ajuste (24-oct-2026): qué
     cambia, de cuánto a cuánto, y para corregir qué. Las opciones se tocan,
     como en la cata; antes eran listas desplegables sin título, el valor de
     ahora era una nota gris y, si faltaba, el formulario quedaba bloqueado. */
  return <div className={styles.adjustmentForm}>
    <h3 className={styles.subTitle}>Tu ajuste para el siguiente café</h3>
    <div className={styles.adjustmentStep}><span>1. Qué cambias</span><div className={styles.choiceRow}>{PROPERTIES.map((option) => <Button key={option.value} variant="quiet" className={chip(property === option.value)} aria-pressed={property === option.value} onPress={() => setProperty(option.value)}>{option.label}</Button>)}</div></div>
    <div className={styles.adjustmentStep}><span>2. De cuánto a cuánto</span><div className={styles.adjustmentValues}>
      <label><small>Ahora ({unit})</small>{recorded !== '' ? <strong>{formatNumber(Number(recorded), Number.isInteger(Number(recorded)) ? 0 : 1)}</strong> : <NumberField step="any" aria-label={`Valor de ahora (${unit})`} placeholder="no se anotó" value={typedPrevious} onChange={(event) => setTypedPrevious(event.target.value)}/>}</label>
      <span aria-hidden="true">→</span>
      <label><small>Próximo café ({unit})</small><NumberField step="any" aria-label={`Valor del próximo café (${unit})`} value={newValue} onChange={(event) => setNewValue(event.target.value)}/></label>
    </div>{same ? <p className={styles.formError} role="alert">El valor nuevo es igual al de ahora.</p> : null}</div>
    {/* Solo con defectos marcados en la cata hay qué elegir; tocar el
        marcado lo desmarca. Sin ninguno, el ajuste apunta a la variable que
        cambia y el paso no se muestra: un «Nada en particular» fijo no decía
        nada. */}
    {defects.length ? <div className={styles.adjustmentStep}><span>3. Qué buscas corregir</span><div className={styles.choiceRow}>{defects.map((value) => <Button key={value} variant="quiet" className={chip(defect === value)} aria-pressed={defect === value} onPress={() => setDefect(defect === value ? '' : value)}>{sensoryDefectLabel(value)}</Button>)}</div></div> : null}
    <div className={styles.adjustmentStep}><span>{modify ? 'Por qué cambias la recomendación' : 'Nota (opcional)'}</span><Input aria-label={modify ? 'Por qué cambias la recomendación' : 'Nota'} maxLength={300} value={reason} onChange={(event) => setReason(event.target.value)}/></div>
    {save.error ? <Alert variant="danger" title="No se pudo registrar el ajuste">{save.error.message}</Alert> : null}
    <div className={styles.recipeActions}>{onCancel ? <Button variant="quiet" className={styles.recipeUndo} onPress={onCancel}>Cancelar</Button> : null}<Button isDisabled={save.isPending || !ready} onPress={() => save.mutate()}>{save.isPending ? 'Guardando…' : 'Registrar ajuste'}</Button></div>
  </div>
}

function RegisteredAdjustment({ episode }: { episode: Episode }) {
  const change = `${propertyLabel(episode.adjustment.property)}: ${formatNumber(episode.adjustment.previousValue, Number.isInteger(episode.adjustment.previousValue) ? 0 : 1)} → ${formatNumber(episode.adjustment.newValue, Number.isInteger(episode.adjustment.newValue) ? 0 : 1)} ${episode.adjustment.unit}`
  return <div className={styles.recommendationBox}>
    <strong>Ajuste registrado · {change}</strong>
    <p className={styles.meta}>{episode.outcome ? `Comprobado: ${OUTCOMES.find((item) => item.value === episode.outcome?.category)?.label ?? episode.outcome.category}.` : episode.followUpExtractionId ? 'Ya tiene su café de comprobación.' : 'Para comprobarlo, en Preparar café elige «De comprobación» en «Clase de preparación» del próximo café.'}</p>
  </div>
}

function CheckOutcome({ episode }: { episode: Episode }) {
  const queryClient = useQueryClient()
  const [category, setCategory] = useState('')
  const [rationale, setRationale] = useState('')
  const assess = useMutation({ mutationFn: () => post<Episode>(`/api/v1/learning-episodes/${episode.id}/outcome`, { category, rationale: rationale.trim() }), onSuccess: () => queryClient.invalidateQueries({ queryKey: ['extraction-history-record'] }) })
  const change = `${propertyLabel(episode.adjustment.property)} ${formatNumber(episode.adjustment.previousValue, 0)} → ${formatNumber(episode.adjustment.newValue, 0)} ${episode.adjustment.unit}`
  if (episode.outcome) return <div className={styles.recommendationBox}><strong>Este café comprobó el ajuste · {change}</strong><p className={styles.meta}>¿Funcionó? {OUTCOMES.find((item) => item.value === episode.outcome?.category)?.label ?? episode.outcome.category}. {episode.outcome.rationale}</p></div>
  return <div className={styles.stack}>
    <strong>Este café comprueba el ajuste · {change}</strong>
    <h3 className={styles.subTitle}>¿Funcionó el ajuste?</h3>
    <div className={styles.choiceRow}>{OUTCOMES.map((option) => { const on = category === option.value; return <Button key={option.value} variant="quiet" className={on ? `${styles.chip} ${styles.chipOn}` : styles.chip} aria-pressed={on} onPress={() => setCategory(option.value)}>{option.label}</Button> })}</div>
    <Field label="Comentario"><TextArea value={rationale} onChange={(event) => setRationale(event.target.value)}/></Field>
    {assess.error ? <Alert variant="danger" title="No se pudo guardar">{assess.error.message}</Alert> : null}
    <div className={styles.recipeActions}><Button isDisabled={!category || !rationale.trim() || assess.isPending} onPress={() => assess.mutate()}>{assess.isPending ? 'Guardando…' : 'Guardar'}</Button></div>
  </div>
}
