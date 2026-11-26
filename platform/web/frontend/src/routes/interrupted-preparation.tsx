/* Una preparación que un corte de energía dejó abierta (ADR-005 § 3 y § 8
   punto 10, RNF-006). El dispositivo la cierra al volver a arrancar y no vio el
   final: no hay serie, indicadores ni duración, y si no tenía hora de pared
   tampoco hay inicio. La pantalla la presenta como incompleta y dice qué falta,
   en lugar de dejar huecos que se lean como ceros o como un fallo de carga.

   No calcula nada: lo que no consta queda sin valor. Tampoco ofrece la
   comparación con el modelo ni el sostenimiento, que no tienen sobre qué
   juzgar. */
import type { Extraction } from '@/shared/api/types'
import { Alert, Measurement } from '@/shared/ui'
import { formatDateTime, terminationLabel } from '@/shared/lib/format'
import styles from './routes.module.css'

export function InterruptedPreparationNotice({ extraction }: { extraction: Pick<Extraction, 'startedAt'> }) {
  return <Alert variant="warning" title="Preparación incompleta">
    Interrumpida por corte de energía; duración desconocida. La máquina la cerró al volver a encender y no registró el final: no hay serie, indicadores ni comparación con el modelo. {extraction.startedAt ? `Empezó ${formatDateTime(extraction.startedAt)}.` : 'La máquina no tenía hora cuando empezó, así que el inicio tampoco consta.'}
  </Alert>
}

export function InterruptedPerformance({ extraction }: { extraction: Pick<Extraction, 'termination' | 'sampleCount'> }) {
  return <div className={styles.measurements}>
    <Measurement label="Duración" value="Desconocida" unit=""/>
    <Measurement label="Causa de finalización" value={terminationLabel(extraction.termination)} unit=""/>
    <Measurement label="Muestras" value={extraction.sampleCount} unit=""/>
  </div>
}
