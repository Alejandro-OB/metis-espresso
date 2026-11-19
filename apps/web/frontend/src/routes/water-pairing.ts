/**
 * Procedencia de la temperatura del cuerpo con que se emparejó una lectura del
 * agua (ADR-019 § 4.3).
 *
 * La lectura se empareja aunque la muestra no sea apta o no sea una medición:
 * la pareja no se descarta ni se sustituye, y quien ajuste la transformación
 * decide qué pares usa. Por eso la pantalla tiene que enseñar de qué muestra
 * salió el cuerpo, y avisar cuando no es una medición válida.
 *
 * Qué no hace: no decide si un par sirve para el ajuste; solo lo señala.
 */

export interface BodyPairing {
  status: 'paired' | 'unpaired'
  quality?: string | null
  temperatureQuality?: string | null
  source?: string | null
}

const SOURCES: Record<string, string> = { measured: 'Medición', simulation: 'Simulación' }

const QUALITIES: Record<string, string> = {
  valid: 'válida',
  invalid: 'no apta',
  simulated: 'simulada',
  missing: 'sin dato',
}

export interface BodyProvenance {
  label: string
  /** Solo una medición con la temperatura válida cuenta como par sin reservas. */
  trusted: boolean
}

export function bodyProvenance(pairing: BodyPairing): BodyProvenance | null {
  if (pairing.status !== 'paired') return null
  // Las lecturas anteriores no guardaban la calidad del canal; entonces vale la
  // de la muestra entera, que es más estricta, nunca una supuesta.
  const quality = pairing.temperatureQuality ?? pairing.quality ?? null
  // Un origen no declarado se lee «desconocido», no «medición».
  const source = pairing.source ?? 'unknown'
  const sourceLabel = SOURCES[source] ?? 'Origen desconocido'
  const qualityLabel = quality ? QUALITIES[quality] ?? quality : 'calidad no declarada'
  return { label: `${sourceLabel} · ${qualityLabel}`, trusted: source === 'measured' && quality === 'valid' }
}
