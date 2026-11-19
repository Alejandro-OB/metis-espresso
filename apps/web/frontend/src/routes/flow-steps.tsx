import type { ReactNode } from 'react'
import { Surface } from '@/shared/ui'
import styles from './routes.module.css'

/* Tres pasos de una misma historia (café → cambio → resultado) en una sola
   tarjeta, numerados y unidos por la línea que los separa (25-oct-2026).
   Antes eran tres tarjetas sueltas con una flecha suelta entre ellas, y se
   leían como tres cosas independientes. Lo usan la página de un ajuste y la
   de una recomendación. */
export function FlowSteps({ children }: { children: ReactNode }) {
  return <Surface><ol className={styles.flowSteps}>{children}</ol></Surface>
}

export function FlowStep({ number, title, children, pending = false }: { number: number; title: string; children: ReactNode; pending?: boolean }) {
  return <li className={pending ? `${styles.flowStep} ${styles.flowStepPending}` : styles.flowStep}>
    <header><span aria-hidden="true">{number}</span><h2>{title}</h2></header>
    <div>{children}</div>
  </li>
}
