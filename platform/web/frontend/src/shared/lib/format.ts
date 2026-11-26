export function formatDateTime(value?: string | null): string {
  if (!value) return 'Sin dato'
  const date = new Date(value)
  return Number.isNaN(date.getTime()) ? 'Fecha inválida' : new Intl.DateTimeFormat('es-CO', { dateStyle: 'medium', timeStyle: 'short' }).format(date)
}

export function formatNumber(value?: number | null, digits = 1): string {
  return value === null || value === undefined ? '—' : new Intl.NumberFormat('es-CO', { maximumFractionDigits: digits, minimumFractionDigits: digits }).format(value)
}

export function duration(value: number | null): string { return value === null ? 'Desconocida' : `${formatNumber(value / 1000, 1)} s` }

export function terminationLabel(value: string): string {
  const labels: Record<string, string> = {
    completed: 'Preparación completada',
    'barista-decision': 'Decisión del barista',
    'acquisition-lost': 'Se dejaron de recibir datos',
    'unexpected-interruption': 'Interrupción inesperada',
    other: 'Otra causa',
    'manual-normal-stop': 'Cierre normal',
    'maximum-extraction-time-reached': 'Tiempo máximo',
    'safety-protection-trip': 'Protección de seguridad',
    'unexpected-power-loss': 'Pérdida de energía',
  }
  return labels[value] ?? value
}
