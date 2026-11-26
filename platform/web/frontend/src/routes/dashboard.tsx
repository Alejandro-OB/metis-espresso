import { useQuery } from '@tanstack/react-query'
import { Link } from 'react-router-dom'
import { LinkButton, Measurement, ServiceStatus, Surface, LoadingState, ErrorState } from '@/shared/ui'
import { api } from '@/shared/api/client'
import type { Coffee, DashboardResponse } from '@/shared/api/types'
import { formatDateTime } from '@/shared/lib/format'
import { MachineStatePanel } from './machine-state'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

function coffeeName(coffee: Coffee | null) {
  if (!coffee) return 'Sin café seleccionado'
  return coffee.commercialReference || coffee.lotIdentifier || coffee.bagIdentifier || 'Café sin nombre'
}

function pendingPath(action: DashboardResponse['pendingActions'][number]) {
  if (action.kind === 'calibration') return '/calibrations'
  const tab = action.kind === 'sensory' ? 'sensory' : 'summary'
  return `/extractions/${action.extractionId}?tab=${tab}`
}

export default function DashboardRoute() {
  const dashboard = useQuery({
    queryKey: ['dashboard'],
    queryFn: () => api<DashboardResponse>('/api/v1/dashboard'),
    refetchInterval: 15_000,
  })
  if (dashboard.isLoading) return <LoadingState label="Cargando inicio" />
  if (dashboard.error || !dashboard.data) return <ErrorState error={dashboard.error ?? new Error('No hay datos disponibles.')} />

  const data = dashboard.data
  const profile = data.activeProfile
  const profileName = typeof profile?.name === 'string' ? profile.name : profile?.id || 'Sin perfil seleccionado'
  const sensoryPending = data.pendingActions.find((action) => action.kind === 'sensory')
  return <div className={styles.page}>
    <PageHeading title="Tu espacio de trabajo" />
    <div className={styles.quickActions} aria-label="Accesos rápidos">
      <LinkButton to="/extract" variant="secondary" icon="coffee">Preparación de práctica</LinkButton>
      <LinkButton to="/calibrations" variant="secondary" icon="plus">Iniciar puesta a punto</LinkButton>
      <LinkButton to={sensoryPending ? pendingPath(sensoryPending) : '/extractions'} variant="secondary" icon="evidence">Registrar evaluación</LinkButton>
      <LinkButton to="/extractions?compare=1" variant="secondary" icon="compare">Comparar</LinkButton>
    </div>

    <div className={styles.grid}>
      <MachineStatePanel/>
      {/* Tres fichas iguales forman la banda de estado: lo que está cargado,
          cuándo se calibró por última vez y qué miden los sensores. «Contexto
          actual» ocupaba dos tercios del ancho para mostrar dos datos y dejaba
          la primera franja de la página medio vacía, que es lo primero que se
          ve al entrar. */}
      <Surface className={styles.span4}>
        <span className={styles.kicker}>Contexto actual</span>
        <div className={styles.contextGrid}>
          <div><span className={styles.contextLabel}>Perfil activo</span><strong>{profileName}</strong>{profile?.version ? <small>Versión {profile.version}</small> : null}</div>
          <div><span className={styles.contextLabel}>Café en uso</span><strong>{coffeeName(data.activeCoffee)}</strong>{data.activeCoffee?.lotIdentifier ? <small>Lote {data.activeCoffee.lotIdentifier}</small> : null}</div>
        </div>
      </Surface>

      <Surface className={styles.span4}>
        <span className={styles.kicker}>Puesta a punto</span>
        <h2 className={styles.surfaceTitle}>Última puesta a punto</h2>
        {data.lastCalibration ? <div className={styles.stack}><strong>{data.lastCalibration.profileName ?? 'Puesta a punto finalizada'}</strong>{data.lastCalibration.finishedAt ? <span className={styles.meta}>{formatDateTime(data.lastCalibration.finishedAt)}</span> : null}</div> : <p className={styles.muted}>Aún no hay puestas a punto registradas.</p>}
      </Surface>

      <Surface className={styles.span4}>
        <span className={styles.kicker}>Sensores</span>
        <h2 className={styles.surfaceTitle}>Lecturas disponibles</h2>
        <div className={styles.sensorList}>{data.sensors.map((sensor) => <div className={styles.sensorRow} key={sensor.id}><div><strong>{sensor.label}</strong><ServiceStatus state={sensor.status}>{sensor.status === 'available' ? 'Disponible' : 'Sin señal'}</ServiceStatus></div><Measurement label="" value={sensor.value ?? '—'} unit={sensor.unit}/></div>)}</div>
        <p className={styles.meta}>{data.lastReadingAt ? `Actualizado ${formatDateTime(data.lastReadingAt)}` : 'Aún no hay lecturas.'}</p>
      </Surface>

      <Surface className={styles.span7}>
        <span className={styles.kicker}>Pendientes</span>
        <h2 className={styles.surfaceTitle}>{data.pendingActions.length ? `${data.pendingActions.length} acciones por completar` : 'Todo está al día'}</h2>
        {data.pendingActions.length ? <div className={styles.compactList}>{data.pendingActions.map((action, index) => <Link to={pendingPath(action)} key={`${action.kind}-${action.extractionId}-${index}`}><span>{action.label}</span><span aria-hidden="true">→</span></Link>)}</div> : <p className={styles.muted}>No tienes actividades pendientes.</p>}
      </Surface>

      <Surface className={styles.span5}>
        <span className={styles.kicker}>Actividad reciente</span>
        <h2 className={styles.surfaceTitle}>Últimas preparaciones</h2>
        <div className={styles.stack}>{data.recentExtractions.map((item) => <div className={styles.service} key={item.id}><div><strong>{item.coffeeReference ?? 'Preparación sin café asociado'}</strong><p>{formatDateTime(item.finishedAt)}</p></div><Link to={`/extractions/${item.id}`} className={styles.rowLink}>Abrir <span aria-hidden="true">→</span></Link></div>)}</div>
      </Surface>
    </div>
  </div>
}
