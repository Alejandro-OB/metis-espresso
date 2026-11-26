import type { ReactNode } from 'react'
import type { ServiceState } from '@/shared/api/types'
import styles from './ui.module.css'

export function Measurement({ label, value, unit }: { label: string; value: string | number; unit: string }) {
  return <div className={styles.measurement}><span className={styles.measurementValue}>{value} <small>{unit}</small></span><span className={styles.measurementLabel}>{label}</span></div>
}

export function ProvenanceBadge({ source }: { source?: string | null | undefined }) {
  const normalized = source === 'measured' ? 'measured' : source === 'simulation' || source === 'synthetic' ? 'simulation' : ''
  const labels: Record<string, string> = { measured: 'Medición física', simulation: 'Simulación', synthetic: 'Dato sintético' }
  return <span className={`${styles.badge} ${styles[normalized]}`}>{labels[source ?? ''] ?? source ?? 'Origen ausente'}</span>
}

export function OriginLabel(props: { source?: string | null | undefined }) { return <ProvenanceBadge {...props} /> }

export function ServiceStatus({ state, children }: { state: ServiceState; children: ReactNode }) {
  return <span className={`${styles.status} ${styles[state]}`}><span className={styles.statusDot}/>{children}</span>
}

export function Uncertainty({ value }: { value: number | null }) {
  return <span className={styles.badge}>{value === null ? 'Incertidumbre no estimada' : `Incertidumbre ${value.toLocaleString('es-CO')}`}</span>
}

export function EvidenceLink({ id }: { id: string }) { return <a href={`#${encodeURIComponent(id)}`} className={styles.badge}>{id}</a> }
