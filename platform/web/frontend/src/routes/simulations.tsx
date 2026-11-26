import { useState } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { Link, useNavigate } from 'react-router-dom'
import { api } from '@/shared/api/client'
import type { Page, PreparationProfile, SimulationModel, SimulationRun } from '@/shared/api/types'
import { Alert, Button, Field, NumberField, Progress, Select, Surface } from '@/shared/ui'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

const statusLabels: Record<string, string> = {
  queued: 'En cola', running: 'En ejecución', succeeded: 'Terminada',
  failed: 'Fallida', cancelled: 'Cancelada', pending: 'Pendiente',
  validating: 'Validando', published: 'Publicada', rejected: 'Rechazada',
}

export default function SimulationsRoute() {
  const [profileId, setProfileId] = useState('')
  const [modelKey, setModelKey] = useState('')
  const [duration, setDuration] = useState('60')
  const navigate = useNavigate()
  const queryClient = useQueryClient()
  const profiles = useQuery({ queryKey: ['profiles', 'simulation-options'], queryFn: () => api<Page<PreparationProfile>>('/api/v1/profiles?page=1&page_size=100') })
  const models = useQuery({ queryKey: ['simulation-models'], queryFn: () => api<{ items: SimulationModel[] }>('/api/v1/simulation-models') })
  const simulations = useQuery({
    queryKey: ['simulations'],
    queryFn: () => api<Page<SimulationRun>>('/api/v1/simulations?page=1&page_size=100'),
    refetchInterval: (query) => query.state.data?.items.some((item) => item.status === 'queued' || item.status === 'running') ? 1500 : false,
  })
  const approvedProfiles = (profiles.data?.items ?? []).filter((profile) => ['approved', 'accepted', 'effective'].includes(profile.status))
  const create = useMutation({
    mutationFn: async () => {
      const [id, version] = modelKey.split('@')
      return api<SimulationRun>('/api/v1/simulations', {
        method: 'POST',
        headers: { 'Idempotency-Key': crypto.randomUUID() },
        body: JSON.stringify({ schemaVersion: '1.0', model: { id, version }, profileId, durationSeconds: Number(duration), controller: { id: 'profile-tracking', version: '1.0.0' } }),
      })
    },
    onSuccess: async (run) => {
      await queryClient.invalidateQueries({ queryKey: ['simulations'] })
      void navigate(`/simulations/${run.simulationRunId}`)
    },
  })
  const optionsError = profiles.error ?? models.error
  return <div className={styles.page}>
    <PageHeading title="Gemelo virtual" detail="Simulaciones con el modelo de la cafetera."/>
    <div className={styles.simulationIntro}>
      <Surface className={styles.simulationLaunch}>
        <form className={styles.simulationForm} onSubmit={(event) => { event.preventDefault(); create.mutate() }}>
          <div className={styles.simulationFormLead}><span className={styles.simulationStamp}>ENTORNO VIRTUAL</span><h2>Preparar una ejecución</h2><p>Los comandos de bomba y calentador existen solo dentro de la FMU. Esta operación no controla la cafetera.</p></div>
          {optionsError ? <Alert variant="danger" title="No fue posible cargar las opciones">{optionsError.message}</Alert> : null}
          <Field label="Perfil aprobado"><Select required value={profileId} onChange={(event) => setProfileId(event.target.value)}><option value="">Selecciona un perfil</option>{approvedProfiles.map((profile) => <option key={profile.id} value={profile.id}>{profile.name} · v{profile.version}</option>)}</Select></Field>
          <Field label="Modelo publicado"><Select required value={modelKey} onChange={(event) => setModelKey(event.target.value)}><option value="">Selecciona un modelo</option>{(models.data?.items ?? []).map((model) => <option key={`${model.id}@${model.version}`} value={`${model.id}@${model.version}`}>{model.displayName} · v{model.version}</option>)}</Select></Field>
          <Field label="Duración simulada" description="De 0,1 a 900 segundos"><NumberField required min="0.1" max="900" step="0.1" value={duration} onChange={(event) => setDuration(event.target.value)} /></Field>
          {create.error ? <Alert variant="danger" title="No se pudo crear la simulación">{create.error.message}</Alert> : null}
          <Button type="submit" icon="arrow" isDisabled={!profileId || !modelKey || create.isPending}>{create.isPending ? 'Creando trabajo…' : 'Ejecutar simulación'}</Button>
        </form>
      </Surface>
    </div>
    <section className={styles.simulationHistory} aria-labelledby="simulation-history"><div className={styles.sectionTitle}><div><span>REGISTRO</span><h2 id="simulation-history">Ejecuciones recientes</h2></div><small>{simulations.data?.total ?? 0} trabajos</small></div>
      {simulations.error ? <Alert variant="danger" title="No fue posible cargar el historial">{simulations.error.message}</Alert> : null}
      <div className={styles.simulationRows}>{(simulations.data?.items ?? []).map((run) => <Link to={`/simulations/${run.simulationRunId}`} className={styles.simulationRow} key={run.simulationRunId}><div><span className={styles.simulationBadge}>SIMULACIÓN</span><strong>{run.profileSnapshot.profileId}</strong><small>{new Date(run.createdAt).toLocaleString('es-CO')}</small></div><div><small>Modelo</small><span>{run.model.id} · {run.model.version}</span></div><div><small>Duración</small><span>{run.durationSeconds.toLocaleString('es-CO')} s</span></div><div><small>Proceso</small><span>{statusLabels[run.status] ?? run.status}</span>{run.status === 'running' ? <Progress value={run.progress.percent} label={`Progreso ${run.progress.percent}%`}/> : null}</div><div><small>Ontología</small><span>{statusLabels[run.semanticStatus] ?? run.semanticStatus}</span></div><span aria-hidden="true">→</span></Link>)}</div>
      {!simulations.isLoading && !simulations.data?.items.length ? <p className={styles.muted}>Aún no hay simulaciones registradas.</p> : null}
    </section>
  </div>
}
