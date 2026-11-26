import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { useQuery, useQueryClient } from '@tanstack/react-query'
import { useAuth } from '@/app/auth-context'
import { api, post, put } from '@/shared/api/client'
import type { Episode, Extraction, ExtractionHistoryRecord, LivePreparation, Page, PreparationProfile } from '@/shared/api/types'
import { Button, Field, Icon, LoadingState, Modal, Select, ServiceStatus, Skeleton, Surface, useToast } from '@/shared/ui'
import { formatNumber } from '@/shared/lib/format'
import { ContextFields } from './context-editor'
import { ExtractionWorkspace, PreparationCurves } from './extraction-detail'
import { LiveChart } from './live-chart'
import { MachineStatePanel } from './machine-state'
import { estimatedWaterText, machineIsOff, STATES, useMachineState } from './machine-state-query'
import { PageHeading } from './page-heading'
import { claimablePreparation, contextPayload, newPreparationContext, nextCupContext, type PreparationContext } from './preparation-context'
import { RecipeEditor, type RecipeController } from './recipe-editor'
import { recipeFrom } from './recipe-values'
import { seriesComplete } from './series-state'
import styles from './routes.module.css'

/* Un café real de principio a fin en una sola página (pedido del usuario del
   19-oct-2026). Arriba la máquina; luego la receta, que son los campos del
   perfil que rige, y el contexto (café, equipos, molienda, notas). El café lo
   inicia siempre el botón de la máquina, nunca esta página. Cuando la placa
   entrega la preparación se le asocian el contexto y la dosis, y aquí mismo se
   ve lo que midió la máquina (no editable) y se completa la taza.

   Al final, «Iniciar preparación» abre una ventana que le dice al barista que
   pulse el botón de la cafetera, dibuja las curvas mientras el café sale y,
   cuando la preparación llega, la muestra completa (pedido del 23-oct-2026).
   El botón no arranca nada: la página no puede encender la bomba, y la espera
   empieza al abrir la página, no al pulsarlo, para no perder un café que se
   pidió antes de abrir la ventana.

   Durante el café la placa manda una muestra de estado por segundo con el
   número de la preparación (`CAFE_TELEMETRY_ACTIVE_INTERVAL_MS`); con ellas se
   dibuja la curva en vivo. La serie completa, más densa, llega al terminar.
   El 19-oct las muestras llegaban cada unos 3 s y la primera a los 8–10 s; la
   curva en vivo enseña lo que llegue, sin rellenar los huecos. */

const DRAFT_KEY = 'espresso:prepare-draft'
// Una preparación que empezó unos segundos antes de abrir la página o de pedir
// otro café sigue siendo esta: el reloj de la placa y el del navegador no
// coinciden al segundo.
const MATCH_MARGIN_MS = 10_000
// Lo que se espera la serie completa después del resumen del café. La placa
// la manda en tramos de 9 muestras, varios por envío y con unos 20 s entre
// envíos: el 26-oct-2026 un café de pocos segundos terminó de llegar 54 s
// después del resumen.
const SERIES_WAIT_MS = 150_000

function readDraft(): PreparationContext {
  try {
    const raw = localStorage.getItem(DRAFT_KEY)
    if (raw) return { ...newPreparationContext, ...JSON.parse(raw) as Partial<PreparationContext> }
  } catch { /* sin almacenamiento local se empieza en blanco */ }
  return newPreparationContext
}

const ADJUSTED: Record<string, string> = { 'grind-setting': 'molienda', 'coffee-dose': 'dosis', 'extraction-time': 'tiempo máximo', 'water-temperature': 'temperatura del agua', 'brew-ratio': 'ratio' }

function adjustmentText(episode: Episode) {
  return `${ADJUSTED[episode.adjustment.property] ?? episode.adjustment.property} ${episode.adjustment.previousValue} → ${episode.adjustment.newValue} ${episode.adjustment.unit}`
}

export default function PrepareRoute() {
  const queryClient = useQueryClient()
  const toast = useToast()
  const { user } = useAuth()
  const initial = useMemo(readDraft, [])
  const [context, setContext] = useState<PreparationContext>(initial)
  const [dose, setDose] = useState('')
  const onDose = useCallback((value: string) => setDose(value), [])
  // Desde cuándo se espera un café: al abrir la página y cada vez que se pide
  // otro. Cualquier preparación medida que empiece después es la de aquí.
  const [watchFrom, setWatchFrom] = useState(() => new Date().toISOString())
  const [preparationId, setPreparationId] = useState<string | null>(null)
  const [startOpen, setStartOpen] = useState(false)
  const recipe = useRef<RecipeController | null>(null)
  const [starting, setStarting] = useState(false)
  // Los cambios del perfil se usan al iniciar, sin un paso aparte. Con el agua
  // fuera de rango no se abre: el editor ya muestra por qué.
  // El botón va junto al título; cuando el barista baja y el de arriba sale
  // de la vista, aparece otro al final de la página (26-oct-2026). Flotando
  // pegado al borde tapaba campos; al final, en su sitio, no tapa nada.
  const topStart = useRef<HTMLSpanElement | null>(null)
  const [topStartVisible, setTopStartVisible] = useState(true)
  useEffect(() => {
    const element = topStart.current
    if (!element || typeof IntersectionObserver === 'undefined') return
    const observer = new IntersectionObserver(([entry]) => setTopStartVisible(entry.isIntersecting))
    observer.observe(element)
    return () => observer.disconnect()
  }, [preparationId])
  const start = async () => {
    const editor = recipe.current
    if (editor?.pending) {
      if (editor.invalid) { toast.push('Revisa el perfil', 'La temperatura del agua está fuera de lo que la máquina alcanza.', 'danger'); return }
      setStarting(true)
      try { await editor.apply() } catch { return } finally { setStarting(false) }
    }
    setStartOpen(true)
  }
  const linked = useRef<string | null>(null)

  useEffect(() => {
    try { localStorage.setItem(DRAFT_KEY, JSON.stringify(context)) } catch { /* solo comodidad */ }
  }, [context])

  // Los ajustes registrados y sin comprobar. El barista dice si este café es
  // de ajuste y de cuál (24-oct-2026); por omisión es un café normal. Antes
  // el último ajuste pendiente se enlazaba solo, con una casilla marcada, y
  // un café cualquiera quedaba como comprobación sin que nadie lo decidiera.
  const episodes = useQuery({ queryKey: ['episodes', 'pending'], queryFn: () => api<Page<Episode>>('/api/v1/learning-episodes?page=1&page_size=100') })
  const pending = (episodes.data?.items ?? []).filter((episode) => episode.status === 'awaiting-verification' && (!user || episode.adjustment.performedBy === user.id)).sort((a, b) => (b.adjustment.registeredAt ?? '').localeCompare(a.adjustment.registeredAt ?? ''))
  const [checkEpisodeId, setCheckEpisodeId] = useState('')
  const profiles = useQuery({ queryKey: ['profiles', 'prepare'], queryFn: () => api<Page<PreparationProfile>>('/api/v1/profiles?page=1&page_size=100') })
  const ruling = profiles.data?.items.find((item) => item.status === 'accepted')

  const latest = useQuery({
    queryKey: ['extractions', 'prepare-latest', watchFrom],
    // Su propia consulta y no el historial (26-oct-2026): el historial de un
    // barista solo trae sus cafés, y el recién llegado aún no tiene barista,
    // así que nunca aparecía. Además, con un filtro el historial completa todas
    // las preparaciones antes de paginar (casi un minuto con 300) y, repetido
    // cada pocos segundos, cargaba la base y retrasaba la telemetría.
    queryFn: () => api<Page<Extraction>>('/api/v1/extractions/claimable?limit=10'),
    refetchInterval: 5_000,
  })
  // Con un café en pantalla se sigue mirando (27-oct-2026): si el barista
  // pulsa otra vez la máquina sin «Preparar otro café», el café nuevo es suyo
  // y con los mismos datos de la preparación. Antes la consulta se apagaba al
  // llegar el primero, y el siguiente quedaba sin barista hasta recargar.
  // Solo cuenta uno que empezó después del que se ve, para no volver a uno
  // anterior.
  const shownStartedAt = useRef<number | null>(null)
  useEffect(() => {
    const items = latest.data?.items ?? []
    if (!preparationId) {
      const found = claimablePreparation(items, Date.parse(watchFrom) - MATCH_MARGIN_MS, user?.id)
      if (found) { shownStartedAt.current = Date.parse(found.startedAt ?? ''); setPreparationId(found.id) }
      return
    }
    const after = shownStartedAt.current
    if (after == null || Number.isNaN(after)) return
    const next = claimablePreparation(items.filter((item) => item.id !== preparationId), after + 1, user?.id)
    if (!next) return
    shownStartedAt.current = Date.parse(next.startedAt ?? '')
    setContext(nextCupContext)
    setCheckEpisodeId('')
    setPreparationId(next.id)
  }, [latest.data, watchFrom, preparationId, user?.id])

  // Al llegar, la preparación recibe el contexto y la dosis que se anotaron.
  useEffect(() => {
    if (!preparationId || linked.current === preparationId) return
    linked.current = preparationId
    const payload = contextPayload(context)
    const tasks: Promise<unknown>[] = []
    if (Object.keys(payload).length) tasks.push(put(`/api/v1/extractions/${preparationId}/context`, payload))
    if (dose) tasks.push(put(`/api/v1/extractions/${preparationId}/manual-measurements`, { coffeeDoseG: Number(dose) }))
    const check = pending.some((episode) => episode.id === checkEpisodeId) ? checkEpisodeId : null
    void Promise.all(tasks)
      .then(() => check ? post(`/api/v1/learning-episodes/${check}/follow-up`, { extractionId: preparationId }) : null)
      .then(() => Promise.all([queryClient.invalidateQueries({ queryKey: ['extraction', preparationId] }), queryClient.invalidateQueries({ queryKey: ['episodes'] }), queryClient.invalidateQueries({ queryKey: ['extraction-history-record'] })]))
      .catch((error: Error) => toast.push('No se pudo completar la preparación', error.message, 'danger'))
  }, [preparationId, context, dose, queryClient, toast, checkEpisodeId, pending])

  // Con un café en pantalla, el sitio de «Iniciar preparación» lo toma
  // «Preparar otro café» (27-oct-2026): estaba entre el panel y el resumen y
  // no se encontraba.
  const anotherButton = <Button variant="secondary" icon="plus" onPress={() => { setPreparationId(null); setContext(nextCupContext); setCheckEpisodeId(''); setWatchFrom(new Date().toISOString()) }}>Preparar otro café</Button>
  const startButton = <Button icon="coffee" onPress={() => void start()} isDisabled={starting}>{starting ? 'Guardando el perfil…' : 'Iniciar preparación'}</Button>
  return <div className={styles.page}>
    <PageHeading title="Preparar café" action={preparationId ? anotherButton : <span ref={topStart}>{startButton}</span>}/>
    <div className={styles.grid}>
      <MachineStatePanel/>
      {preparationId ? null : <>
        <Surface className={styles.span12}>
          <h2 className={styles.surfaceTitle}>Perfil</h2>
          {ruling ? <RecipeEditor ruling={ruling} profiles={profiles.data?.items ?? []} coffeeId={context.coffeeId} onDose={onDose} controller={recipe}/> : <p className={styles.muted}>{profiles.isLoading ? 'Cargando el perfil que rige…' : 'La máquina no tiene un perfil vigente. Crea uno en Perfiles.'}</p>}
        </Surface>
        <Surface className={styles.span12}>
          <h2 className={styles.surfaceTitle}>Datos de la preparación</h2>
          {/* Siempre a la vista, para que el barista sepa que existe; sin un
              ajuste por comprobar, la opción de ajuste sale desactivada y dice
              por qué, en vez de no aparecer. */}
          <ContextFields value={context} onChange={setContext} autoEquipment leading={<div className={styles.fieldUniform}><Field label="Clase de preparación" description={pending.length ? 'el de ajuste lo comprueba' : 'sin ajustes por comprobar'}><Select value={checkEpisodeId} onChange={(event) => setCheckEpisodeId(event.target.value)}>
            <option value="">Normal</option>
            {pending.length ? pending.map((episode) => <option key={episode.id} value={episode.id}>De comprobación: {adjustmentText(episode)}</option>) : <option value="none" disabled>De comprobación (no hay ajustes por comprobar)</option>}
          </Select></Field></div>}/>
        </Surface>
        {/* Oculto sin quitarlo, para que la página no cambie de alto. */}
        <div className={`${styles.span12} ${styles.startAction} ${topStartVisible ? styles.startActionHidden : ''}`} aria-hidden={topStartVisible || undefined}>{startButton}</div>
      </>}
    </div>
    <StartPreparationModal isOpen={startOpen} onOpenChange={setStartOpen} preparationId={preparationId} ruling={ruling} grind={context.grindSetting} dose={dose}/>
    {preparationId ? <>
      <ExtractionWorkspace id={preparationId} single/>
    </> : null}
  </div>
}

// Tres momentos en la misma ventana: la instrucción mientras la máquina
// espera; las curvas en vivo mientras sale el café; y, cuando la preparación
// llega, sus curvas completas y el paso siguiente.
function StartPreparationModal({ isOpen, onOpenChange, preparationId, ruling, grind, dose }: { isOpen: boolean; onOpenChange: (open: boolean) => void; preparationId: string | null; ruling: PreparationProfile | undefined; grind: string; dose: string }) {
  // La serie completa puede llegar después del resumen del café (services.sync):
  // la ventana vuelve a preguntar cada 2 s hasta que trae lecturas, y se rinde
  // al minuto y medio. Antes preguntaba una sola vez, al llegar el resumen, y
  // mostraba «no están disponibles» aunque las lecturas venían en camino.
  const [arrivedAt, setArrivedAt] = useState<number | null>(null)
  const [, setGaveUp] = useState(false)
  useEffect(() => { setArrivedAt(preparationId ? Date.now() : null); setGaveUp(false) }, [preparationId])
  // Un render al cumplirse la espera, para que el aviso dé paso al mensaje.
  useEffect(() => {
    if (!arrivedAt) return
    const timer = window.setTimeout(() => setGaveUp(true), SERIES_WAIT_MS)
    return () => window.clearTimeout(timer)
  }, [arrivedAt])
  const waitingSeries = (data: ExtractionHistoryRecord | undefined) => Boolean(arrivedAt && Date.now() - arrivedAt < SERIES_WAIT_MS && !seriesComplete(data))
  const record = useQuery({ queryKey: ['extraction-history-record', preparationId], queryFn: () => api<ExtractionHistoryRecord>(`/api/v1/extractions/${preparationId}/history-record`), enabled: isOpen && Boolean(preparationId), refetchInterval: (query) => waitingSeries(query.state.data) ? 2_000 : false })
  const live = useQuery({ queryKey: ['live-preparation'], queryFn: () => api<LivePreparation>('/api/v1/machine-state/live-preparation'), enabled: isOpen && !preparationId, refetchInterval: 1_000 })
  const running = live.data?.active ? live.data : null
  // Lo último que se vio en vivo se queda en pantalla mientras llega la serie:
  // es el mismo café, a una lectura por segundo, y evita una espera en blanco.
  const [held, setHeld] = useState<LivePreparation | null>(null)
  useEffect(() => { if (running) setHeld(running) }, [running])
  useEffect(() => { if (isOpen && !preparationId) setHeld(null) }, [isOpen, preparationId])
  // Lo visto en vivo es del café que estaba en curso; si la página pasa de un
  // café a otro sin volver a esperar, ya no corresponde.
  const shownId = useRef(preparationId)
  useEffect(() => {
    if (shownId.current && preparationId && shownId.current !== preparationId) setHeld(null)
    shownId.current = preparationId
  }, [preparationId])
  const title = preparationId ? 'Tu café está listo' : running ? 'Preparando tu café' : 'Iniciar preparación'
  return <Modal isOpen={isOpen} onOpenChange={onOpenChange} title={title} wide={Boolean(preparationId || running)}>
    {preparationId ? <div className={styles.stack}>
      {!waitingSeries(record.data) ? <PreparationCurves record={record.data} loading={record.isLoading} error={record.error}/> : held ? <Surface><h2 className={styles.surfaceTitle}>Curvas</h2><LiveCurves live={held} ruling={ruling} finished/></Surface> : record.data?.series.samples.length ? <PreparationCurves record={record.data} loading={false} error={null}/> : <CurvesSkeleton/>}
      <div className={styles.recipeActions}><Button onPress={() => onOpenChange(false)}>Anotar la taza y la cata</Button></div>
    </div> : running ? <LiveCurves live={running} ruling={ruling}/> : <StartWaiting ruling={ruling} grind={grind} dose={dose}/>}
  </Modal>
}

// Antes de pulsar, lo que el barista mira para decidir si ya puede: si la
// máquina está lista, el agua que saldría ahora y lo que eligió para este
// café. Recién cambiado el perfil, la máquina sigue unos segundos con el
// objetivo anterior hasta que se entera; mientras tanto no se da por lista,
// aunque su estado lo diga, porque el café saldría con el agua vieja.
function StartWaiting({ ruling, grind, dose }: { ruling: PreparationProfile | undefined; grind: string; dose: string }) {
  const machine = useMachineState().data
  const profile = ruling ? recipeFrom(ruling) : null
  const wanted = profile ? Number(profile.water) : Number.NaN
  const caughtUp = machine?.waterTargetC == null || !Number.isFinite(wanted) || Math.abs(machine.waterTargetC - wanted) < 0.05
  const ready = Boolean(machine?.connected && machine.state === 'ready' && caughtUp)
  const off = machineIsOff(machine)
  const state = !machine ? '—' : off ? 'Apagada' : !machine.connected ? 'Sin señal' : !caughtUp ? 'Tomando el perfil' : (machine.state && STATES[machine.state]?.label) ?? 'Desconocido'
  const number = (value: number, unit: string) => `${formatNumber(value, Number.isInteger(value) ? 0 : 1)} ${unit}`
  return <div className={styles.startWaiting}>
    <Icon name="coffee" size={40}/>
    <p className={styles.startInstruction}>{ready ? 'Para iniciar la preparación, presiona el botón de café de tu cafetera.' : off ? 'Enciende la máquina para realizar una extracción.' : 'Espera a que la máquina esté lista para presionar el botón de café.'}</p>
    <ServiceStatus state={ready ? 'available' : machine?.connected ? 'degraded' : 'unavailable'}>Máquina: {state.toLowerCase()}</ServiceStatus>
    <dl className={styles.startFacts}>
      <div><dt>Temperatura estimada del agua</dt><dd>{estimatedWaterText(machine) ? `${estimatedWaterText(machine)} °C` : '—'}</dd></div>
      <div><dt>Molienda</dt><dd>{grind ? number(Number(grind), 'clics') : '—'}</dd></div>
      <div><dt>Dosis</dt><dd>{dose ? number(Number(dose), 'g') : '—'}</dd></div>
      <div><dt>Tiempo máximo</dt><dd>{profile?.maxTime ? number(Number(profile.maxTime), 's') : '—'}</dd></div>
    </dl>
  </div>
}

// Las bandas son las del perfil que rige, como en las curvas completas; sin
// tolerancia declarada, las mismas de respaldo que usan esas curvas.
function LiveCurves({ live, ruling, finished = false }: { live: LivePreparation; ruling: PreparationProfile | undefined; finished?: boolean }) {
  const tolerance = (properties: string[], fallback: number) => ruling?.specifications?.find((item) => properties.includes(item.property))?.tolerance ?? fallback
  const temperatureTarget = live.boilerReferenceC ?? 93
  const pressureTarget = live.pressureTargetBar ?? 9
  const temperatureTolerance = tolerance(['water-temperature'], 1)
  const pressureTolerance = tolerance(['extraction-pressure', 'pressure'], 0.5)
  return <div className={styles.stack}>
    {live.samples.length ? <div className={styles.liveCharts}>
      <LiveChart samples={live.samples} field="temperatureC" title="Temperatura de la caldera" unit="°C" target={temperatureTarget} tolerance={temperatureTolerance} minimum={Math.min(85, temperatureTarget - temperatureTolerance * 3)} maximum={Math.max(110, temperatureTarget + temperatureTolerance * 3)}/>
      <LiveChart samples={live.samples} field="pressureBar" title="Presión" unit="bar" target={pressureTarget} tolerance={pressureTolerance} minimum={0} maximum={Math.max(12, pressureTarget + pressureTolerance * 3)}/>
    </div> : <LoadingState label="Esperando la primera lectura"/>}
    {finished ? null : <p className={styles.muted}>Al terminar, aquí aparece la preparación completa.</p>}
  </div>
}

// Mientras llega la serie, el hueco de las dos gráficas con su brillo de
// carga, en el mismo lugar y del mismo tamaño: sin texto que explicar y sin
// que la ventana salte cuando aparecen.
function CurvesSkeleton() {
  return <Surface><h2 className={styles.surfaceTitle}>Curvas</h2><div className={styles.liveCharts} role="status" aria-label="Cargando las curvas"><Skeleton height="14.5rem"/><Skeleton height="14.5rem"/></div></Surface>
}
