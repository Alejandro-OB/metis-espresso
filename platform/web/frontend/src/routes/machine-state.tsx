import { useEffect, useState } from 'react'
import { Measurement, ServiceStatus, Surface } from '@/shared/ui'
import { formatDateTime, formatNumber } from '@/shared/lib/format'
import { estimatedWaterText, machineIsOff, STATES, useMachineState } from './machine-state-query'
import styles from './routes.module.css'

// Más de dos minutos de diferencia ya no es la red: la placa tiene la hora
// mal y fecharía los cafés con ella (26-oct-2026, 10 h atrás al arrancar).
const CLOCK_TOLERANCE_S = 120
// Desde el firmware v9 la placa corrige sola la hora en la siguiente
// sincronización, unos segundos. El aviso solo sale si pasado un minuto sigue
// mal: antes salía de inmediato y pedía reiniciar algo que se arreglaba solo.
const CLOCK_GRACE_MS = 60_000

function clockShift(seconds: number): string {
  const total = Math.abs(seconds)
  const hours = Math.floor(total / 3600)
  const minutes = Math.round((total % 3600) / 60)
  return hours ? `${hours} h ${minutes} min` : `${minutes} min`
}


function elapsed(since: string, now: number): string {
  const seconds = Math.max(0, Math.floor((now - Date.parse(since)) / 1000))
  const hours = Math.floor(seconds / 3600)
  const minutes = Math.floor((seconds % 3600) / 60)
  const rest = seconds % 60
  if (hours) return `${hours} h ${minutes} min`
  if (minutes) return `${minutes} min ${rest} s`
  return `${rest} s`
}

// El estado de la máquina en vivo: qué hace, desde cuándo y por qué. Si la
// señal se pierde no se muestra el último estado como si siguiera vigente.

export function MachineStatePanel() {
  const machine = useMachineState()
  const [now, setNow] = useState(() => Date.now())
  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), 1_000)
    return () => window.clearInterval(timer)
  }, [])
  const data = machine.data
  const known = data?.state ? STATES[data.state] : undefined
  const clockWrong = Boolean(data?.connected && data.boardClockBehindS != null && Math.abs(data.boardClockBehindS) > CLOCK_TOLERANCE_S)
  const [clockWrongSince, setClockWrongSince] = useState<number | null>(null)
  useEffect(() => { setClockWrongSince((since) => clockWrong ? since ?? Date.now() : null) }, [clockWrong])
  const clockAlert = clockWrong && clockWrongSince != null && now - clockWrongSince > CLOCK_GRACE_MS
  return <Surface className={styles.span12}>
    <span className={styles.kicker}>Máquina</span>
    {!data ? <p className={styles.muted}>Consultando la máquina…</p> : !data.connected ? machineIsOff(data) ? <>
      <h2 className={styles.surfaceTitle}><ServiceStatus state="unavailable">Apagada</ServiceStatus></h2>
      <p>Enciéndela para realizar una extracción.</p>
      <p className={styles.muted}>Si acabas de encenderla, espera unos segundos a que se conecte.</p>
    </> : data.reachable ? <>
      <h2 className={styles.surfaceTitle}><ServiceStatus state="degraded">Sin lecturas</ServiceStatus></h2>
      <p>La máquina está encendida pero no envía lecturas. Apágala y vuelve a encenderla.</p>
    </> : <>
      <h2 className={styles.surfaceTitle}><ServiceStatus state="unavailable">Sin señal</ServiceStatus></h2>
      <p className={styles.muted}>{data.lastTelemetryAt ? `La última lectura llegó ${formatDateTime(data.lastTelemetryAt)}.` : 'La máquina todavía no ha enviado lecturas.'}</p>
    </> : <>
      <h2 className={styles.surfaceTitle}><ServiceStatus state={known?.tone ?? 'degraded'}>{known?.label ?? data.state ?? 'Estado desconocido'}</ServiceStatus>{data.since ? <small> · desde hace {elapsed(data.since, now)}</small> : null}</h2>
      {data.state === 'alarm' && data.reason ? <p role="alert">{data.reason.description ?? data.reason.code}</p> : null}
      {machineIsOff(data) ? <p>Enciéndela para realizar una extracción.</p> : null}
      {clockAlert && data.boardClockBehindS != null ? <p role="alert" className={styles.formError}>La hora de la máquina está {data.boardClockBehindS > 0 ? 'atrasada' : 'adelantada'} {clockShift(data.boardClockBehindS)}. Apágala y vuelve a encenderla antes de preparar.</p> : null}
      {/* Primero el agua, que es lo que el barista pide en el perfil; luego la
          caldera, de donde sale la estimación. El calentador se quitó
          (26-oct-2026): mientras regula se prende y se apaga dentro de cada
          ventana de 1 s (ADR-009), así que la lectura es un instante al azar,
          y el barista no tiene nada que hacer con ese dato. */}
      <div className={styles.measurements}>
        <Measurement label="Temperatura estimada del agua" value={estimatedWaterText(data) ?? '—'} unit="°C"/>
        <Measurement label="Temperatura objetivo del agua" value={data.waterTargetC == null ? '—' : formatNumber(data.waterTargetC, 1)} unit="°C"/>
        <Measurement label="Temperatura actual de la caldera" value={data.bodyTemperatureC == null ? '—' : formatNumber(data.bodyTemperatureC, 1)} unit="°C"/>
        <Measurement label="Temperatura objetivo de la caldera" value={data.boilerReferenceC == null ? '—' : formatNumber(data.boilerReferenceC, 1)} unit="°C"/>
        <Measurement label="Presión actual" value={data.pressureBar == null ? '—' : formatNumber(data.pressureBar, 1)} unit="bar"/>
        <Measurement label="Presión objetivo" value={data.pressureTargetBar == null ? '—' : formatNumber(data.pressureTargetBar, 1)} unit="bar"/>
      </div>
    </>}
  </Surface>
}
