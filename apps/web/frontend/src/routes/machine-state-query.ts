import { useQuery } from '@tanstack/react-query'
import { api } from '@/shared/api/client'
import type { ServiceState } from '@/shared/api/types'
import { formatNumber } from '@/shared/lib/format'

/* El estado de la máquina que muestran el panel de Preparar café y la ventana
   de iniciar. Aparte del componente para que los dos lo importen sin romper
   la recarga en caliente. */

export interface MachineState {
  connected: boolean
  lastTelemetryAt: string | null
  state: string | null
  since?: string | null
  // «telemetry»: no llegó el aviso de cambio y el instante es el de la primera
  // lectura en este estado, con unos segundos de holgura.
  sinceSource?: 'event' | 'telemetry' | null
  reason?: { code: string; description: string | null; severity: string | null } | null
  bodyTemperatureC?: number | null
  // El agua que saldría si se pulsara ahora (recta del acta v6 sobre la caldera).
  estimatedWaterC?: number | null
  // Con la caldera fuera del rango medido de la recta: de qué lado y el borde.
  estimatedWaterBound?: { side: 'above' | 'below'; limitC: number } | null
  waterTargetC?: number | null
  boilerReferenceC?: number | null
  pressureBar?: number | null
  pressureTargetBar?: number | null
  heaterOn?: boolean | null
  boardClockBehindS?: number | null
  // Sin lecturas: false, no contesta en la red (apagada o sin red); true,
  // contesta pero no envía; null, no se pudo probar.
  reachable?: boolean | null
}

// Apagada con su botón (la placa sigue con corriente y lo dice) o sin
// corriente (no contesta en la red). Las dos piden lo mismo al barista.
export function machineIsOff(machine: MachineState | undefined): boolean {
  if (!machine) return false
  return machine.connected ? machine.state === 'safe-inactive' : machine.reachable === false
}

// Los estados son los del firmware (`telemetry.schema.json`).
export const STATES: Record<string, { label: string; tone: ServiceState }> = {
  'safe-inactive': { label: 'Apagada', tone: 'unavailable' },
  'coffee-heating': { label: 'Calentando', tone: 'degraded' },
  ready: { label: 'Lista', tone: 'available' },
  extraction: { label: 'Preparando café', tone: 'available' },
  'manual-purge': { label: 'Purgando', tone: 'available' },
  'steam-heating': { label: 'Calentando para vapor', tone: 'degraded' },
  'steam-ready': { label: 'Lista para vapor', tone: 'available' },
  'cooling-to-brew': { label: 'Enfriando para café', tone: 'degraded' },
  alarm: { label: 'En alarma', tone: 'unavailable' },
}

// Una sola consulta para el panel y la ventana de iniciar: comparten la clave
// y React Query no pregunta dos veces.
export function useMachineState() {
  return useQuery({ queryKey: ['machine-state'], queryFn: () => api<MachineState>('/api/v1/machine-state'), refetchInterval: 2_000 })
}

// «92,2», o «más de 92,5» cuando la caldera pasa del rango medido: en «Lista»
// es lo común, y un «—» se leía como que no había dato.
export function estimatedWaterText(machine: MachineState | undefined): string | null {
  if (!machine?.connected) return null
  if (machine.estimatedWaterC != null) return formatNumber(machine.estimatedWaterC, 1)
  const bound = machine.estimatedWaterBound
  return bound ? `${bound.side === 'above' ? 'más de' : 'menos de'} ${formatNumber(bound.limitC, 1)}` : null
}
