import { useEffect, useState } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { useNavigate, useParams } from 'react-router-dom'
import { api, post, put } from '@/shared/api/client'
import type { Extraction, ExtractionAlertPreferences, LiveExtraction } from '@/shared/api/types'
import { Alert, Button, Checkbox, ErrorState, Field, LoadingState, Select, Surface, TextArea, useToast } from '@/shared/ui'
import { formatDateTime } from '@/shared/lib/format'
import { PageHeading } from './page-heading'
import { LiveChart } from './live-chart'
import styles from './routes.module.css'

const causes = [
  ['completed', 'Preparación completada'],
  ['barista-decision', 'Decisión del barista'],
  ['acquisition-lost', 'Se dejaron de recibir datos'],
  ['unexpected-interruption', 'Interrupción inesperada'],
  ['other', 'Otra causa'],
] as const

function elapsedLabel(milliseconds: number) {
  const totalSeconds = Math.floor(milliseconds / 1000)
  return `${String(Math.floor(totalSeconds / 60)).padStart(2, '0')}:${String(totalSeconds % 60).padStart(2, '0')}.${Math.floor(milliseconds % 1000 / 100)}`
}

export default function LiveExtractionRoute() {
  const { id } = useParams()
  const navigate = useNavigate()
  const queryClient = useQueryClient()
  const toast = useToast()
  const [now, setNow] = useState(Date.now())
  const [cause, setCause] = useState<(typeof causes)[number][0]>('completed')
  const [detail, setDetail] = useState('')
  const [preferences, setPreferences] = useState<ExtractionAlertPreferences>({ temperature: true, pressure: true, acquisition: true })
  const preferenceQuery = useQuery({ queryKey: ['extraction-alert-preferences'], queryFn: () => api<ExtractionAlertPreferences>('/api/v1/me/extraction-alerts') })
  const live = useQuery({
    queryKey: ['live-extraction', id],
    queryFn: () => api<LiveExtraction>(`/api/v1/extractions/${id}/live`),
    enabled: Boolean(id),
    refetchInterval: (query) => query.state.data?.status === 'running' ? 500 : false,
  })
  const finish = useMutation({
    mutationFn: () => post<Extraction>(`/api/v1/extractions/${id}/finish`, { cause, detail: detail || null }),
    onSuccess: async (item) => {
      if (item.verificationForEpisodeId) {
        await queryClient.invalidateQueries({ queryKey: ['episode', item.verificationForEpisodeId] })
        void navigate(`/episodes/${item.verificationForEpisodeId}`)
        return
      }
      void navigate(`/extractions/${item.id}?completed=1`)
    },
  })
  const savePreferences = useMutation({
    mutationFn: () => put<ExtractionAlertPreferences>('/api/v1/me/extraction-alerts', {
      temperature: preferences.temperature,
      pressure: preferences.pressure,
      acquisition: preferences.acquisition,
    }),
    onSuccess: async () => {
      await Promise.all([
        queryClient.invalidateQueries({ queryKey: ['extraction-alert-preferences'] }),
        queryClient.invalidateQueries({ queryKey: ['live-extraction', id] }),
      ])
      toast.push('Avisos actualizados', 'La preferencia se aplicará a tus preparaciones.', 'success')
    },
  })
  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), 100)
    return () => window.clearInterval(timer)
  }, [])
  useEffect(() => {
    if (preferenceQuery.data) setPreferences(preferenceQuery.data)
  }, [preferenceQuery.data])
  if (live.isLoading) return <LoadingState label="Cargando preparación"/>
  if (live.error) return <ErrorState error={live.error}/>
  const item = live.data
  if (!item) return null
  const elapsed = item.status === 'running' ? Math.max(0, now - Date.parse(item.startedAt)) : Math.max(0, item.samples.at(-1)?.elapsedMs ?? 0)
  const lastSample = item.samples.at(-1)
  const temperatureTarget = item.profile.targets.temperatureC ?? 93
  const pressureTarget = item.profile.targets.pressureBar ?? 9
  const temperatureTolerance = item.profile.tolerances.temperatureC ?? 1
  const pressureTolerance = item.profile.tolerances.pressureBar ?? 0.5

  return <div className={styles.page}>
    <PageHeading title={item.coffeeReference || 'Preparación de práctica'} detail={`${item.profile.name || item.profile.id} · versión ${item.profile.version}`}/>
    <p className={styles.simulationNotice}>Datos simulados para pruebas. El registro observa las mediciones disponibles y no controla la cafetera.</p>
    <div className={styles.liveObservations} aria-live="polite" aria-label="Avisos de la preparación">{item.observations.map((observation) => <Alert variant="warning" title={observation.message} key={`${observation.code}-${observation.observedAt}`}>{observation.observedValue === undefined ? 'Revisa si vuelven a recibirse lecturas.' : `Valor observado: ${observation.observedValue} ${observation.unit}. Límite del perfil: ${observation.limit} ${observation.unit}.`}</Alert>)}</div>
    <div className={styles.liveSummary}>
      <Surface><span className={styles.contextLabel}>Tiempo desde el inicio</span><strong className={styles.liveClock}>{elapsedLabel(elapsed)}</strong><small>Incluye la preinfusión</small></Surface>
      <Surface aria-live="polite"><span className={styles.contextLabel}>Recepción de datos</span><strong>{lastSample?.quality === 'valid' ? 'Disponible' : 'Con interrupciones'}</strong><small>{item.lastSampleAt ? `Última lectura: ${formatDateTime(item.lastSampleAt)}` : 'Esperando la primera lectura'}</small><small>{item.samples.length.toLocaleString('es-CO')} lecturas recibidas</small></Surface>
      <Surface><span className={styles.contextLabel}>Ambiente</span><strong>{item.environment.temperatureC == null ? 'Sin datos' : `${item.environment.temperatureC} °C`}</strong><small>{item.environment.humidityPercent == null ? 'Humedad sin registrar' : `${item.environment.humidityPercent} % de humedad`}</small></Surface>
    </div>
    <div className={styles.liveCharts}>
      <LiveChart samples={item.samples} field="temperatureC" title="Temperatura" unit="°C" target={temperatureTarget} tolerance={temperatureTolerance} minimum={Math.min(85, temperatureTarget - temperatureTolerance * 3)} maximum={Math.max(100, temperatureTarget + temperatureTolerance * 3)}/>
      <LiveChart samples={item.samples} field="pressureBar" title="Presión" unit="bar" target={pressureTarget} tolerance={pressureTolerance} minimum={0} maximum={Math.max(12, pressureTarget + pressureTolerance * 3)}/>
    </div>
    <Surface><details className={styles.alertPreferences}><summary>Configurar mis avisos</summary><p>Elige qué observaciones quieres ver durante tus preparaciones.</p><div><Checkbox label="Temperatura fuera del perfil" checked={preferences.temperature} onChange={(event) => setPreferences((current) => ({ ...current, temperature: event.target.checked }))}/><Checkbox label="Presión fuera del perfil" checked={preferences.pressure} onChange={(event) => setPreferences((current) => ({ ...current, pressure: event.target.checked }))}/><Checkbox label="Lecturas no disponibles" checked={preferences.acquisition} onChange={(event) => setPreferences((current) => ({ ...current, acquisition: event.target.checked }))}/></div><Button variant="secondary" onPress={() => savePreferences.mutate()} isDisabled={savePreferences.isPending}>{savePreferences.isPending ? 'Guardando…' : 'Guardar avisos'}</Button>{savePreferences.error ? <Alert variant="danger" title="No se guardaron los avisos">{savePreferences.error.message}</Alert> : null}</details></Surface>
    <Surface><form className={styles.finishExtraction} onSubmit={(event) => { event.preventDefault(); finish.mutate() }}><div><h2 className={styles.surfaceTitle}>Finalizar registro</h2><p className={styles.formHint}>Indica por qué terminó la preparación.</p></div><Field label="Causa"><Select value={cause} onChange={(event) => setCause(event.target.value as typeof cause)}>{causes.map(([value, label]) => <option value={value} key={value}>{label}</option>)}</Select></Field>{cause === 'other' ? <Field label="Descripción"><TextArea required value={detail} onChange={(event) => setDetail(event.target.value)}/></Field> : null}<Button type="submit" isDisabled={finish.isPending}>{finish.isPending ? 'Finalizando…' : 'Finalizar preparación'}</Button>{finish.error ? <Alert variant="danger" title="No se pudo finalizar">{finish.error.message}</Alert> : null}</form></Surface>
  </div>
}
