import { useState } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { Link, useParams } from 'react-router-dom'
import { api, post } from '@/shared/api/client'
import type { Calibration, Coffee, Extraction, Page } from '@/shared/api/types'
import { Alert, Button, Field, Input, NumberField, Select, Surface, TextArea, useToast } from '@/shared/ui'
import { formatDateTime } from '@/shared/lib/format'
import { Breadcrumbs } from '@/shared/ui/layout'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

const propertyOptions = [
  ['water-temperature', 'Temperatura del agua'], ['pressure', 'Presión'], ['extraction-time', 'Tiempo'],
  ['coffee-dose', 'Dosis'], ['brew-ratio', 'Relación de preparación'], ['grind-setting', 'Molienda'],
] as const
const resultLabels: Record<string, string> = { 'validated-profile': 'Perfil vigente validado', 'new-profile-version': 'Nueva versión del perfil', rejected: 'Puesta a punto rechazada' }
const eventLabels: Record<string, string> = { started: 'Puesta a punto iniciada', 'test-added': 'Preparación de prueba registrada', 'adjustment-added': 'Ajuste registrado', closed: 'Puesta a punto finalizada' }

export default function CalibrationDetailRoute() {
  const { id = '' } = useParams()
  const queryClient = useQueryClient()
  const toast = useToast()
  const [extractionId, setExtractionId] = useState('')
  const [property, setProperty] = useState('water-temperature')
  const [previousValue, setPreviousValue] = useState('')
  const [newValue, setNewValue] = useState('')
  const [unit, setUnit] = useState('dial')
  const [adjustmentReason, setAdjustmentReason] = useState('')
  const [result, setResult] = useState('rejected')
  const [closeReason, setCloseReason] = useState('')
  const calibration = useQuery({ queryKey: ['calibration', id], queryFn: () => api<Calibration>(`/api/v1/calibrations/${id}`) })
  const coffees = useQuery({ queryKey: ['coffees', 'calibration-detail'], queryFn: () => api<Page<Coffee>>('/api/v1/coffees?page=1&page_size=100') })
  const extractions = useQuery({ queryKey: ['extractions', 'calibration-options'], queryFn: () => api<Page<Extraction>>('/api/v1/extractions?page=1&page_size=100') })
  const refresh = async () => { await Promise.all([queryClient.invalidateQueries({ queryKey: ['calibration', id] }), queryClient.invalidateQueries({ queryKey: ['calibrations'] })]) }
  const addTest = useMutation({ mutationFn: () => post<Calibration>(`/api/v1/calibrations/${id}/tests`, { extractionId }), onSuccess: async () => { await refresh(); setExtractionId(''); toast.push('Prueba registrada', 'La preparación se añadió a la secuencia.', 'success') } })
  const addAdjustment = useMutation({ mutationFn: () => post<Calibration>(`/api/v1/calibrations/${id}/adjustments`, { property, previousValue: Number(previousValue), newValue: Number(newValue), unit, rationale: adjustmentReason }), onSuccess: async () => { await refresh(); setPreviousValue(''); setNewValue(''); setAdjustmentReason(''); toast.push('Ajuste registrado', 'Ahora puedes preparar y añadir otra prueba.', 'success') } })
  const close = useMutation({ mutationFn: () => post<Calibration>(`/api/v1/calibrations/${id}/close`, { result, rationale: closeReason }), onSuccess: async () => { await refresh(); toast.push('Puesta a punto finalizada', 'El resultado quedó registrado.', 'success') } })
  if (calibration.isLoading) return <p>Cargando puesta a punto…</p>
  if (calibration.error || !calibration.data) return <Alert variant="danger" title="No fue posible cargar la puesta a punto">{calibration.error?.message}</Alert>
  const item = calibration.data
  const coffee = coffees.data?.items.find((candidate) => candidate.id === item.coffeeId)
  const availableExtractions = (extractions.data?.items ?? []).filter((extraction) => extraction.coffeeId === item.coffeeId && !item.testExtractionIds.includes(extraction.id)).sort((a, b) => (a.startedAt ?? '').localeCompare(b.startedAt ?? ''))
  const error = addTest.error ?? addAdjustment.error ?? close.error
  return <div className={styles.page}>
    <Breadcrumbs items={[{ label: 'Puestas a punto', to: '/calibrations' }, { label: formatDateTime(item.startedAt) }]}/>
    <PageHeading title="Detalle de la puesta a punto" detail={`${coffee?.commercialReference ?? coffee?.lotIdentifier ?? item.coffeeId}${item.profileName ? ` · ${item.profileName}` : ' · sin perfil previo'}`}/>
    <div className={styles.calibrationLayout}>
      <Surface><h2 className={styles.surfaceTitle}>Secuencia</h2><ol className={styles.timeline}>{item.events.map((event) => <li key={event.sequence}><div><strong>{eventLabels[event.kind]}</strong><span>{formatDateTime(event.occurredAt)}</span>{event.extractionId ? <Link to={`/extractions/${event.extractionId}`}>{event.extractionId}</Link> : null}{event.adjustmentId ? <span>{item.adjustments.find((adjustment) => adjustment.id === event.adjustmentId)?.rationale}</span> : null}{event.result ? <span>{resultLabels[event.result]}</span> : null}</div></li>)}</ol></Surface>
      <Surface><h2 className={styles.surfaceTitle}>Contexto</h2><dl className={styles.definition}><dt>Café</dt><dd>{coffee?.commercialReference ?? coffee?.lotIdentifier ?? item.coffeeId}</dd><dt>Perfil previo</dt><dd>{item.profileName ?? 'No se seleccionó'}</dd><dt>Lugar</dt><dd>{item.context.station || 'Sin información'}</dd><dt>Responsable</dt><dd>{item.startedBy}</dd>{item.context.notes ? <><dt>Notas</dt><dd>{item.context.notes}</dd></> : null}</dl></Surface>
    </div>
    {item.status === 'open' ? <>
      {error ? <Alert variant="danger" title="No fue posible guardar">{error.message}</Alert> : null}
      <Surface><h2 className={styles.surfaceTitle}>Añadir preparación de prueba</h2><form className={styles.inlineWorkflowForm} onSubmit={(event) => { event.preventDefault(); addTest.mutate() }}><Field label="Preparación"><Select required value={extractionId} onChange={(event) => setExtractionId(event.target.value)}><option value="">Selecciona una preparación</option>{availableExtractions.map((extraction) => <option value={extraction.id} key={extraction.id}>{extraction.coffeeReference ?? extraction.id} · {formatDateTime(extraction.startedAt)}</option>)}</Select></Field><Button type="submit" isDisabled={!extractionId || addTest.isPending}>Añadir prueba</Button></form></Surface>
      <Surface><h2 className={styles.surfaceTitle}>Registrar ajuste</h2><p className={styles.formHint}>Modifica una variable y prepara otra extracción para observar el resultado.</p><form className={styles.adjustmentGrid} onSubmit={(event) => { event.preventDefault(); addAdjustment.mutate() }}><Field label="Variable"><Select value={property} onChange={(event) => setProperty(event.target.value)}>{propertyOptions.map(([value, label]) => <option value={value} key={value}>{label}</option>)}</Select></Field><Field label="Valor anterior"><NumberField required step="any" value={previousValue} onChange={(event) => setPreviousValue(event.target.value)}/></Field><Field label="Valor nuevo"><NumberField required step="any" value={newValue} onChange={(event) => setNewValue(event.target.value)}/></Field><Field label="Unidad"><Input required value={unit} onChange={(event) => setUnit(event.target.value)}/></Field><div className={styles.formFull}><Field label="Motivo del ajuste"><TextArea required value={adjustmentReason} onChange={(event) => setAdjustmentReason(event.target.value)}/></Field></div><div className={`${styles.actions} ${styles.formFull}`}><Button type="submit" isDisabled={!item.testExtractionIds.length || addAdjustment.isPending}>Registrar ajuste</Button></div></form></Surface>
      <Surface><h2 className={styles.surfaceTitle}>Finalizar puesta a punto</h2><form className={styles.closeCalibrationForm} onSubmit={(event) => { event.preventDefault(); close.mutate() }}><Field label="Resultado"><Select value={result} onChange={(event) => setResult(event.target.value)}><option value="rejected">Rechazar puesta a punto</option>{item.profileId ? <option value="validated-profile">Validar perfil vigente</option> : null}{item.profileId ? <option value="new-profile-version">Crear nueva versión</option> : null}</Select></Field><Field label="Conclusión"><TextArea required value={closeReason} onChange={(event) => setCloseReason(event.target.value)}/></Field><Button type="submit" isDisabled={!item.testExtractionIds.length || close.isPending}>Finalizar</Button></form></Surface>
    </> : <Surface><h2 className={styles.surfaceTitle}>Resultado</h2><p><strong>{resultLabels[item.result ?? '']}</strong></p><p>{item.rationale}</p>{item.resultProfileId ? <Link to={`/profiles/${item.resultProfileId}`}>Ver perfil relacionado</Link> : null}</Surface>}
  </div>
}
