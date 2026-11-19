import { useState } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { api, post } from '@/shared/api/client'
import type { DashboardResponse, Page } from '@/shared/api/types'
import { Alert, Button, DataTable, Field, Input, Pagination, Select, Surface, TextArea, type Column, useToast } from '@/shared/ui'
import { downloadAttachment } from '@/shared/lib/download'
import { formatDateTime } from '@/shared/lib/format'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'
import { bodyProvenance } from './water-pairing'

/**
 * Lecturas del agua de salida con un termómetro de referencia (ADR-019, guion 8.1).
 *
 * Cada lectura se empareja en la plataforma con la temperatura del cuerpo de la
 * caldera que midió el dispositivo en ese instante. Por eso el instante se toma
 * **al pulsar «Registrar»**, no se escribe a mano: un minuto de error en la hora
 * emparejaría el agua con un cuerpo que ya no estaba así.
 *
 * Ese instante lo sella la plataforma al recibir la lectura, no el navegador: el
 * dispositivo toma la hora de la plataforma, y un móvil desfasado un par de
 * segundos dejaba la lectura sin pareja (ADR-019 § 6.2). La hora del navegador
 * se envía solo como referencia, y si se aparta del margen se avisa.
 *
 * Qué no hace: no pide la temperatura del cuerpo, que sale de la serie del
 * dispositivo, ni estima la del agua; solo recoge lo que se midió.
 */

type Condition = 'rest' | 'after-extraction' | 'during-extraction'

interface WaterReference {
  id: string
  deviceId: string
  takenAt: string
  declaredTakenAt: string | null
  clockOffsetMs: number | null
  waterTemperatureC: number
  instrument: string
  condition: Condition
  extractionId: string | null
  extractionIdSource?: 'declared' | 'telemetry' | null
  notes: string | null
  pairing: { status: 'paired' | 'unpaired'; bodyTemperatureC?: number | null; gapMs?: number; reason?: string; quality?: string | null; temperatureQuality?: string | null; source?: string | null }
}

const CONDITIONS: Record<Condition, string> = {
  rest: 'En reposo',
  'after-extraction': 'Justo después de una extracción',
  'during-extraction': 'Durante una extracción',
}

// Un periodo de telemetría: por encima, la hora del navegador habría dejado la
// lectura sin pareja si fuera ella la que emparejara.
const CLOCK_WARNING_MS = 1000

const INSTRUMENT_DEFAULT = 'DS18B20, sonda de referencia (0.5 y 0.7 del guion)'

export default function WaterReferencesRoute() {
  const [page, setPage] = useState(1)
  const queryClient = useQueryClient()
  const toast = useToast()
  const readings = useQuery({ queryKey: ['water-references', page], queryFn: () => api<Page<WaterReference>>(`/api/v1/water-references?page=${page}`) })
  const dashboard = useQuery({ queryKey: ['dashboard'], queryFn: () => api<DashboardResponse>('/api/v1/dashboard') })
  const lastDevice = dashboard.data?.lastReadingDeviceId ?? ''

  const [deviceId, setDeviceId] = useState('')
  const [water, setWater] = useState('')
  const [instrument, setInstrument] = useState(INSTRUMENT_DEFAULT)
  const [condition, setCondition] = useState<Condition>('rest')
  const [extractionId, setExtractionId] = useState('')
  const [notes, setNotes] = useState('')

  const record = useMutation({
    mutationFn: (payload: Record<string, unknown>) => post<WaterReference>('/api/v1/water-references', payload),
    onSuccess: async (item) => {
      setWater('')
      await queryClient.invalidateQueries({ queryKey: ['water-references'] })
      const provenance = bodyProvenance(item.pairing)
      toast.push('Lectura registrada', provenance
        ? `Emparejada con el cuerpo a ${item.pairing.bodyTemperatureC?.toFixed(2)} °C (${provenance.label}).`
        : 'Sin muestra del cuerpo en ese instante: revisa que el dispositivo esté enviando datos.', provenance?.trusted ? 'success' : 'warning')
      if (item.clockOffsetMs != null && Math.abs(item.clockOffsetMs) > CLOCK_WARNING_MS) {
        toast.push('El reloj de este equipo está desfasado', `Se aparta ${(item.clockOffsetMs / 1000).toFixed(1)} s de la plataforma. La lectura usa la hora de la plataforma, pero conviene ajustar el reloj.`, 'warning')
      }
    },
  })

  const device = deviceId || lastDevice
  const needsExtraction = condition === 'during-extraction'
  // La preparación ya no se exige aquí: si falta, la plataforma la toma de la
  // muestra emparejada, y solo rechaza la lectura si la máquina tampoco la dice.
  const valid = Boolean(device) && water !== '' && Number.isFinite(Number(water)) && Boolean(instrument)

  const columns: Column<WaterReference>[] = [
    { id: 'takenAt', header: 'Instante', cell: (item) => formatDateTime(item.takenAt) },
    { id: 'condition', header: 'Condición', cell: (item) => CONDITIONS[item.condition] },
    { id: 'water', header: 'Temperatura del agua', cell: (item) => `${item.waterTemperatureC.toFixed(2)} °C`, numeric: true },
    { id: 'body', header: 'Cuerpo', cell: (item) => item.pairing.status === 'paired' && item.pairing.bodyTemperatureC != null ? `${item.pairing.bodyTemperatureC.toFixed(2)} °C` : 'Sin emparejar', numeric: true },
    { id: 'provenance', header: 'Procedencia del cuerpo', cell: (item) => {
      const provenance = bodyProvenance(item.pairing)
      if (!provenance) return '—'
      return provenance.trusted ? provenance.label : `${provenance.label} (revisar)`
    } },
    { id: 'gap', header: 'Desfase', cell: (item) => item.pairing.status === 'paired' && item.pairing.gapMs != null ? `${item.pairing.gapMs} ms` : '—', numeric: true },
    { id: 'extraction', header: 'Preparación', cell: (item) => item.extractionId ? `${item.extractionId}${item.extractionIdSource === 'telemetry' ? ' (de la máquina)' : ''}` : '—' },
  ]

  return <div className={styles.page}>
    <PageHeading title="Lecturas del agua de salida"/>
    <Surface>
      <h2 className={styles.surfaceTitle}>Registrar una lectura</h2>
      <p className={styles.formHint}>Pulsa «Registrar» en el momento en que lees el termómetro: la hora se toma al pulsar, no se escribe. La máquina debe estar conectada y enviando datos para que la lectura se empareje.</p>
      <form className={styles.formGrid} onSubmit={(event) => {
        event.preventDefault()
        record.mutate({
          deviceId: device,
          // Solo como referencia: el instante que empareja lo sella la plataforma.
          takenAt: new Date().toISOString(),
          waterTemperatureC: Number(water),
          instrument,
          condition,
          extractionId: extractionId || null,
          notes: notes || null,
        })
      }}>
        <Field label="Temperatura del agua (°C)"><Input inputMode="decimal" required value={water} onChange={(event) => setWater(event.target.value.replace(',', '.'))} placeholder="91.4"/></Field>
        <Field label="Condición"><Select value={condition} onChange={(event) => setCondition(event.target.value as Condition)}>{Object.entries(CONDITIONS).map(([value, label]) => <option key={value} value={value}>{label}</option>)}</Select></Field>
        <Field label="Preparación" description={needsExtraction ? 'Si la dejas vacía, se toma de la máquina en el instante de la lectura' : 'Opcional'}><Input value={extractionId} onChange={(event) => setExtractionId(event.target.value)} placeholder="Identificador de la preparación"/></Field>
        <Field label="Dispositivo" description={lastDevice ? `Por omisión, el último que envió una lectura: ${lastDevice}` : 'Ninguno conocido todavía'}><Input value={deviceId} onChange={(event) => setDeviceId(event.target.value)} placeholder={lastDevice}/></Field>
        <div className={styles.formFull}><Field label="Instrumento"><Input required value={instrument} onChange={(event) => setInstrument(event.target.value)}/></Field></div>
        <div className={styles.formFull}><Field label="Notas" description="Opcional: dónde estaba la sonda, cuánta agua salió, etc."><TextArea value={notes} onChange={(event) => setNotes(event.target.value)}/></Field></div>
        {record.error ? <div className={styles.formFull}><Alert variant="danger" title="No se pudo registrar">{record.error.message}</Alert></div> : null}
        <div className={`${styles.actions} ${styles.formFull}`}><Button type="submit" isDisabled={!valid || record.isPending}>{record.isPending ? 'Registrando…' : 'Registrar'}</Button></div>
      </form>
    </Surface>
    <Surface>
      <h2 className={styles.surfaceTitle}>Lecturas registradas</h2>
      {readings.data?.items.some((item) => bodyProvenance(item.pairing)?.trusted === false)
        ? <Alert variant="warning" title="Hay pares que no salen de una medición válida">Las marcadas «revisar» se emparejaron con una muestra no apta o sin origen de medición. Se conservan así, sin sustituirlas: quien ajuste la transformación decide si las usa.</Alert>
        : null}
      {readings.error ? <Alert variant="danger" title="No fue posible cargar las lecturas">{readings.error.message}</Alert> : <DataTable label="Lecturas del agua" rows={readings.data?.items ?? []} columns={columns} getKey={(item) => item.id} loading={readings.isLoading}/>}
      <Pagination page={readings.data?.page ?? page} pages={readings.data?.pages ?? 1} onChange={setPage}/>
      {/* Todas las lecturas, también las sin pareja: son la evidencia del ajuste de la transformación (ADR-019 § 4.2). */}
      <div className={styles.profileDetailActions}><Button variant="secondary" icon="download" onPress={() => downloadAttachment('/api/v1/water-references/export')}>Descargar pares en CSV</Button></div>
    </Surface>
  </div>
}
