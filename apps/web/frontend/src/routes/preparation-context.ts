import type { Coffee, Extraction } from '@/shared/api/types'

export function coffeeName(coffee: Coffee) {
  return coffee.commercialReference || coffee.lotIdentifier || coffee.bagIdentifier || coffee.id
}

export interface PreparationContext {
  coffeeId: string
  machineId: string
  grinderId: string
  grindSetting: string
  notes: string
  // El cuarto lo anota quien prepara (sonda R del protocolo de catación § 3.2):
  // la placa no lo mide.
  ambientTemperatureC: string
  ambientHumidityPercent: string
  sessionCode: string
  blindCode: string
  block: string
  condition: string
  // Con qué se preparó: el gemelo replica la preparación con la carga hidráulica
  // de esa configuración. En uso normal siempre es café (el agua sola es de los
  // ensayos de puesta en servicio), así que el formulario la trae marcada y se
  // ve; quien prepara la cambia si no. Una preparación vieja sin el dato sigue
  // sin él: vacío es «no consta».
  portafilterConfiguration: string
}

export const emptyContext: PreparationContext = {
  coffeeId: '', machineId: '', grinderId: '', grindSetting: '', notes: '',
  ambientTemperatureC: '', ambientHumidityPercent: '', sessionCode: '', blindCode: '', block: '', condition: '',
  portafilterConfiguration: '',
}

// El contexto con que empieza una preparación nueva: café, como en todo uso normal.
export const newPreparationContext: PreparationContext = { ...emptyContext, portafilterConfiguration: 'pressurized-double-coffee' }

// Lo que es de una sola taza: al pedir otro café se borra para que la taza
// siguiente no herede el código ciego ni la condición de la anterior, que
// afirmarían algo que nadie anotó para ella. La sesión, el bloque y el cuarto
// siguen valiendo.
export function nextCupContext(context: PreparationContext): PreparationContext {
  return { ...context, blindCode: '', condition: '' }
}

export function contextFromExtraction(item: Extraction): PreparationContext {
  const session = item.tastingSession ?? {}
  const number = (value: number | null | undefined) => value == null ? '' : String(value)
  return {
    coffeeId: item.coffeeId ?? '',
    machineId: item.machineId ?? '',
    grinderId: item.grinderId ?? '',
    grindSetting: number(item.grindSetting),
    notes: item.notes ?? '',
    ambientTemperatureC: number(item.environment?.temperatureC),
    ambientHumidityPercent: number(item.environment?.humidityPercent),
    sessionCode: session.sessionCode ?? '',
    blindCode: session.blindCode ?? '',
    block: session.block ?? '',
    condition: session.condition ?? '',
    portafilterConfiguration: item.portafilterConfiguration ?? '',
  }
}

// Solo se manda lo que tiene valor: el servidor cambia lo que llega y deja lo
// demás como estaba, así que un campo vacío no borra nada.
export function contextPayload(context: PreparationContext): Record<string, unknown> {
  const payload: Record<string, unknown> = {}
  if (context.coffeeId) payload.coffeeId = context.coffeeId
  if (context.machineId) payload.machineId = context.machineId
  if (context.grinderId) payload.grinderId = context.grinderId
  if (context.grindSetting !== '') payload.grindSetting = Number(context.grindSetting)
  if (context.notes.trim()) payload.notes = context.notes.trim()
  if (context.ambientTemperatureC !== '') payload.ambientTemperatureC = Number(context.ambientTemperatureC)
  if (context.ambientHumidityPercent !== '') payload.ambientHumidityPercent = Number(context.ambientHumidityPercent)
  if (context.portafilterConfiguration) payload.portafilterConfiguration = context.portafilterConfiguration
  const session: Record<string, string> = {}
  if (context.sessionCode.trim()) session.sessionCode = context.sessionCode.trim()
  if (context.blindCode.trim()) session.blindCode = context.blindCode.trim().toUpperCase()
  if (context.block) session.block = context.block
  if (context.condition.trim()) session.condition = context.condition.trim()
  if (Object.keys(session).length) payload.tastingSession = session
  return payload
}

// La preparación medida que «Preparar café» toma como la suya: empezó después
// de `startMs` y no tiene autor (recién llegada de la placa) o es del usuario
// de la sesión. Con dos cuentas a la vez, tomar la última de cualquiera le
// pondría a uno el café, la dosis y la autoría de la taza del otro.
export function claimablePreparation(items: Extraction[], startMs: number, userId: string | undefined): Extraction | undefined {
  return items.find((item) => item.source === 'measured' && item.startedAt !== null && Date.parse(item.startedAt) >= startMs && (!item.barista || item.barista === userId))
}
